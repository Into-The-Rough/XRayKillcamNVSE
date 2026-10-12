"""Fit the donor horse skeleton into the HAP horse's rest pose: every bone's shell placed in the horse's skin space with its region
and the rig bone it rides, written for tools/build_horse.py.

The donors are assets/source/horse-donors.zip and the organs tools/horse_organs.py reduced into horse-organ-donors.zip. The reference is
the HAP horse from your own mod folder, its skeleton.nif and horse_creature.nif, which never leave your machine:

    HAP_HORSE=<folder with skeleton.nif and horse_creature.nif> python tools/horse_fit.py"""
import hashlib
import json
import os
import sys
import zipfile
from pathlib import Path

import numpy
from scipy import ndimage
from scipy.spatial import cKDTree
from pyffi.formats.nif import NifFormat

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import human_rest

SOURCE = ROOT / 'assets/source'
OUTPUT = SOURCE / 'extracted/horse/fitted.json'

#the donor's shells in order of first appearance in its face list
LABELS = ['right humerus', 'left humerus', 'right femur', 'left femur', 'right patella', 'left patella', 'right tarsus', 'left tarsus',
          'right tibia', 'left tibia', 'right tarsus', 'left tarsus', 'right fibula', 'left fibula', 'right hind cannon', 'left hind cannon',
          'right front cannon', 'left front cannon', 'right hind P1', 'left hind P1', 'right hind P2', 'left hind P2', 'right front P2',
          'right front P1', 'left front P2', 'left front P1', 'right front P3', 'left front P3', 'right hind P3', 'left hind P3',
          *['right tarsus'] * 3, *['left tarsus'] * 3, *['right carpus'] * 9, *['left carpus'] * 9, 'right radius', 'left radius',
          'Cd1', 'Cd2', *[f'Cd{n}' for n in range(18, 2, -1)], 'right scapula', 'left scapula', *['lower teeth'] * 6, 'mandible',
          *['lower teeth'] * 12, 'pelvis', 'C7', 'C6', 'C5', 'C4', 'C3', 'C1', 'C2', 'L6', 'L5', 'L4', 'L1', 'L2', 'L3',
          'T8', 'T7', 'T6', 'T5', *[f'T{n}' for n in range(9, 19)], 'T4', 'T1', 'T3', 'T2', 'sternum', *['cartilage'] * 17,
          *['rib'] * 36, 'sacrum', *['upper teeth'] * 18, 'skull']
FACES = [2904, 2906, 2914, 2896, 246, 248, 904, 904, 2734, 2736, 442, 444, 230, 232, 1844, 1846, 1836, 1868, 334, 338, 370, 374, 384, 502,
         378, 494, 356, 362, 362, 356, 180, 144, 160, 182, 148, 154, 120, 128, 104, 106, 110, 102, 104, 108, 150, 118, 128, 108, 106, 108,
         110, 102, 112, 144, 2008, 2008, 280, 240, 76, 70, 62, 66, 74, 76, 86, 96, 104, 122, 104, 118, 152, 168, 198, 254, 1986, 1986, 110,
         124, 110, 124, 102, 104, 4378, 116, 116, 128, 128, 138, 138, 130, 130, 126, 126, 126, 126, 6760, 1298, 1946, 1946, 2206, 1946, 652,
         1446, 1288, 1288, 1288, 1288, 1288, 1066, 1274, 1180, 1230, 1268, 1210, 1202, 1178, 1104, 1046, 1042, 1066, 1146, 1106, 1162, 1306,
         1198, 1276, 1254, 2282, 136, 134, 148, 148, 160, 160, 174, 174, 200, 200, 424, 412, 410, 1470, 216, 206, 1447, 312, 358, 376, 408,
         418, 436, 452, 444, 408, 328, 374, 364, 328, 322, 302, 322, 322, 274, 312, 352, 370, 416, 418, 436, 446, 450, 396, 362, 324, 372,
         336, 336, 326, 314, 284, 292, 2606, 152, 154, 152, 152, 166, 168, 146, 146, 160, 160, 176, 172, 96, 96, 98, 96, 94, 94, 9128]
#the tail past the croup swings on the tail bones no region carries
KEPT_TAIL = 4
#the trunk's scale across and along the body and its offset, the largest that keeps the trunk 1.5 units under the skin
TRUNK = (20.25, 19.75, 11.5, 71.0)
#the head's scale, pitch in degrees, condyle position and its height and width against its length, the largest within 1 unit of the
#skin with the mouth cavity counted inside
HEAD = dict(scale=23.75, pitch=-3.0, ty=68.0, tz=127.5, squash=0.8, narrow=0.8)
CLEARANCE, ANGLES, STEP = 0.6, 36, 0.5
VOXEL = 0.5
TAU = 2 * numpy.pi


def donors(archive_name, manifest_name):
    """Every mesh of a donor archive, checked against its manifest, by its file name."""
    out = {}
    with zipfile.ZipFile(SOURCE / archive_name) as archive:
        for item in json.loads((SOURCE / manifest_name).read_text())['files']:
            data = archive.read(item['path'])
            if hashlib.sha256(data).hexdigest() != item['sha256']:
                raise RuntimeError('source checksum mismatch: ' + item['path'])
            vertices, faces = [], []
            for line in data.decode().splitlines():
                if line.startswith('v '):
                    vertices.append(tuple(map(float, line.split()[1:4])))
                elif line.startswith('f '):
                    faces.append([int(t.split('/')[0]) - 1 for t in line.split()[1:]])
            out[Path(item['path']).stem] = (numpy.array(vertices), numpy.array(faces))
    return out


def donor():
    v, f = donors('horse-donors.zip', 'horse-manifest.json')['skeleton']
    #donor x left, y up, z forward to the horse's x right, y forward, z up
    return numpy.c_[-v[:, 0], v[:, 2], v[:, 1]], f


def shells(vertices, faces):
    parent = numpy.arange(len(vertices))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for f in faces:
        r = find(f[0])
        for i in f[1:]:
            s = find(i)
            if s != r:
                parent[s] = r
    roots = numpy.array([find(f[0]) for f in faces])
    order = list(dict.fromkeys(roots.tolist()))
    out = []
    for r in order:
        f = faces[roots == r]
        used = numpy.unique(f)
        remap = numpy.full(len(vertices), -1)
        remap[used] = numpy.arange(len(used))
        out.append({'v': vertices[used], 'f': remap[f]})
    if [len(s['f']) for s in out] != FACES:
        raise RuntimeError('donor shells differ from the labelled ones')
    return out


class Reference:
    """The HAP horse's rest skeleton and its body's skin as an inside distance field."""

    def __init__(self, folder):
        skeleton = NifFormat.Data()
        with open(folder / 'skeleton.nif', 'rb') as f:
            skeleton.read(f)
        self.rest = human_rest.rest_world(skeleton)
        body = NifFormat.Data()
        with open(folder / 'horse_creature.nif', 'rb') as f:
            body.read(f)
        shape = next(s for s in body.get_global_iterator() if isinstance(s, NifFormat.NiTriBasedGeom) and s.name.decode() == 'Horse Body')
        for bone, data in zip(shape.skin_instance.bones, shape.skin_instance.data.bone_list):
            #the anatomy is placed in skin space, which only matches skeleton space while the binds are the rest pose
            if numpy.abs(numpy.linalg.inv(human_rest.transform(data.skin_transform)) - self.rest[bone.name.decode()]).max() > 1e-3:
                raise RuntimeError('horse body bind differs from the skeleton rest: ' + bone.name.decode())
        self.vertices = numpy.array([[p.x, p.y, p.z] for p in shape.data.vertices])
        self.triangles = numpy.array([list(t) for t in shape.data.get_triangles()])
        self.lo = self.vertices.min(0) - 2
        self.shape = numpy.ceil((self.vertices.max(0) + 2 - self.lo) / VOXEL).astype(int) + 1
        self.inside = self.voxelise(self.vertices, self.triangles)
        #the mouth is modelled inside the head, the teeth may sit in it
        filled = self.inside.copy()
        for y in range(filled.shape[1]):
            filled[:, y, :] = ndimage.binary_fill_holes(filled[:, y, :])
        for x in range(filled.shape[0]):
            filled[x] = ndimage.binary_fill_holes(filled[x])
        self.distance = ndimage.distance_transform_edt(filled) * VOXEL

    def joint(self, name):
        return self.rest[name][:3, 3].copy()

    def voxelise(self, v, t):
        """The voxels inside a closed mesh, by the crossings of each line along x."""
        shape, lo = self.shape, self.lo
        hits = [[] for _ in range(shape[1] * shape[2])]
        for a, b, c in zip(v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]):
            y0 = int(numpy.ceil((min(a[1], b[1], c[1]) - lo[1]) / VOXEL)); y1 = int(numpy.floor((max(a[1], b[1], c[1]) - lo[1]) / VOXEL))
            z0 = int(numpy.ceil((min(a[2], b[2], c[2]) - lo[2]) / VOXEL)); z1 = int(numpy.floor((max(a[2], b[2], c[2]) - lo[2]) / VOXEL))
            d = (b[1] - a[1]) * (c[2] - a[2]) - (c[1] - a[1]) * (b[2] - a[2])
            if y1 < y0 or z1 < z0 or abs(d) < 1e-12:
                continue
            gy, gz = numpy.meshgrid(numpy.arange(y0, y1 + 1), numpy.arange(z0, z1 + 1), indexing='ij')
            py = lo[1] + gy * VOXEL + 1e-4; pz = lo[2] + gz * VOXEL + 1e-4
            u = ((py - a[1]) * (c[2] - a[2]) - (c[1] - a[1]) * (pz - a[2])) / d
            w = ((b[1] - a[1]) * (pz - a[2]) - (py - a[1]) * (b[2] - a[2])) / d
            m = (u >= 0) & (w >= 0) & (u + w <= 1)
            for iy, iz, x in zip(gy[m], gz[m], (a[0] + u * (b[0] - a[0]) + w * (c[0] - a[0]))[m]):
                hits[iy * shape[2] + iz].append(x)
        inside = numpy.zeros(shape, bool)
        for iy in range(shape[1]):
            for iz in range(shape[2]):
                h = sorted(hits[iy * shape[2] + iz])
                for x0, x1 in zip(h[0::2], h[1::2]):
                    i0 = int(numpy.ceil((x0 - lo[0]) / VOXEL)); i1 = int(numpy.floor((x1 - lo[0]) / VOXEL))
                    if i1 >= i0:
                        inside[i0:i1 + 1, iy, iz] = True
        return inside

    def sample(self, field, points):
        i = numpy.round((numpy.asarray(points) - self.lo) / VOXEL).astype(int)
        ok = (i >= 0).all(1) & (i < numpy.array(field.shape)).all(1)
        out = numpy.zeros(len(i))
        out[ok] = field[i[ok, 0], i[ok, 1], i[ok, 2]]
        return out

    def depth(self, points):
        return self.sample(self.distance, points)

    def limb(self, name):
        """A limb pivot moved to the middle of the limb's own skin along y at its height, the pivot itself where the limb meets the body."""
        p = self.joint(name)
        ix, iy, iz = numpy.round((p - self.lo) / VOXEL).astype(int)
        column = self.inside[ix, :, iz]
        if not column[iy]:
            candidates = numpy.flatnonzero(column)
            iy = candidates[numpy.argmin(numpy.abs(candidates - iy))]
        a = b = iy
        while a > 0 and column[a - 1]:
            a -= 1
        while b < len(column) - 1 and column[b + 1]:
            b += 1
        if (b - a) * VOXEL >= 20:
            return p
        return numpy.array([p[0], self.lo[1] + (a + b) / 2 * VOXEL, p[2]])


def joints(parts):
    """Each joint is the middle of where its two bones come within 0.06 donor units of each other."""
    def verts(label):
        return numpy.vstack([s['v'] for s, l in parts if l == label])

    def contact(a, b):
        A, B = verts(a), verts(b)
        da, _ = cKDTree(B).query(A)
        db, _ = cKDTree(A).query(B)
        d = 0.06
        while True:
            near = numpy.vstack([A[da < d], B[db < d]])
            if len(near) >= 12:
                return near.mean(0)
            d *= 1.5
    out = {}
    for side in ('left', 'right'):
        for name, a, b in (('shoulder', 'scapula', 'humerus'), ('elbow', 'humerus', 'radius'), ('carpus', 'radius', 'carpus'),
                           ('front fetlock', 'front cannon', 'front P1'), ('front coffin', 'front P2', 'front P3'),
                           ('hip', 'femur', None), ('stifle', 'femur', 'tibia'), ('hock', 'tibia', 'tarsus'),
                           ('hind fetlock', 'hind cannon', 'hind P1'), ('hind coffin', 'hind P2', 'hind P3')):
            out[f'{side} {name}'] = contact(f'{side} {a}', f'{side} {b}' if b else 'pelvis')
    chain = ['skull', 'C1', 'C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'T1']
    for a, b in zip(chain, chain[1:]):
        out[f'{a}-{b}'] = contact(a, b)
    return out


def similarity(scale, offset):
    return lambda p: numpy.asarray(p) * numpy.asarray(scale) + numpy.asarray(offset)


def head_map(pivot):
    a = numpy.radians(HEAD['pitch'])
    rotation = numpy.array([[1, 0, 0], [0, numpy.cos(a), -numpy.sin(a)], [0, numpy.sin(a), numpy.cos(a)]])
    scale = numpy.array([HEAD['narrow'], 1, HEAD['squash']]) * HEAD['scale']
    return lambda p: ((numpy.asarray(p) - pivot) * scale) @ rotation.T + numpy.array([0, HEAD['ty'], HEAD['tz']])


def frame(axis, forward):
    u = axis / numpy.linalg.norm(axis)
    f = forward - u * (forward @ u)
    f /= numpy.linalg.norm(f)
    return numpy.stack([u, f, numpy.cross(u, f)])


def segment(d0, d1, g0, g1, radial=None, anchor=None):
    """Carries the donor segment d0-d1 onto the game segment g0-g1, stretched along it and scaled across it by the trunk's
    scale, or the segment's rotation at the trunk's scale with the donor anchor on the game anchor."""
    d0, d1, g0, g1 = map(numpy.asarray, (d0, d1, g0, g1))
    forward = numpy.array([0.0, 1.0, 0.0])
    D, G = frame(d1 - d0, forward), frame(g1 - g0, forward)
    across = TRUNK[0] if radial is None else radial
    if anchor is not None:
        k = numpy.diag([across] * 3)
        return lambda p: anchor[1] + ((numpy.asarray(p) - anchor[0]) @ D.T) @ k @ G
    k = numpy.diag([numpy.linalg.norm(g1 - g0) / numpy.linalg.norm(d1 - d0), across, across])
    return lambda p: g0 + ((numpy.asarray(p) - d0) @ D.T) @ k @ G


def cage(parts, start, end, depth, passes=12):
    """Pulls the parts toward the segment axis until they sit inside the skin, smoothly over a level by angle grid."""
    start = numpy.asarray(start, float)
    axis = numpy.asarray(end, float) - start
    length = numpy.linalg.norm(axis)
    axis /= length
    side = numpy.cross(axis, [0, 0, 1] if abs(axis[2]) < .9 else [1, 0, 0])
    side /= numpy.linalg.norm(side)
    front = numpy.cross(axis, side)
    levels = int(length / STEP) + 3

    def index(p):
        d = p - start
        h = d @ axis
        r = d - numpy.outer(h, axis)
        return numpy.clip(h / STEP + 1, 0, levels - 1), (numpy.arctan2(r @ front, r @ side) % TAU) * ANGLES / TAU, numpy.linalg.norm(r, axis=1), h
    radii = numpy.full((levels, ANGLES), 1e9)
    march = numpy.arange(0, 40, 0.25)
    for k in range(levels):
        centre = start + axis * ((k - 1) * STEP)
        for a in range(ANGLES):
            t = a * TAU / ANGLES
            inside = depth(centre + numpy.outer(march, side * numpy.cos(t) + front * numpy.sin(t))) > 0
            if not inside[0]:
                radii[k, a] = 0
            elif not inside.all():
                radii[k, a] = numpy.argmax(~inside) * 0.25
    remaining = []
    for _ in range(passes):
        factors = numpy.ones((levels, ANGLES))
        for p in parts:
            h, a, r, _ = index(p)
            hi, ai = numpy.rint(h).astype(int), numpy.rint(a).astype(int) % ANGLES
            m = r > 0.01
            numpy.minimum.at(factors, (hi[m], ai[m]), (radii[hi[m], ai[m]] - CLEARANCE) / r[m])
        factors = numpy.clip(factors, 0.3, 1)
        for _ in range(2):
            factors = (factors + numpy.roll(factors, 1, 1) + numpy.roll(factors, -1, 1)
                       + numpy.vstack([factors[:1], factors[:-1]]) + numpy.vstack([factors[1:], factors[-1:]])) / 5
        for n, p in enumerate(parts):
            h, a, r, along = index(p)
            h0 = numpy.floor(h).astype(int); h1 = numpy.minimum(levels - 1, h0 + 1); fh = h - h0
            a0 = numpy.floor(a).astype(int) % ANGLES; a1 = (a0 + 1) % ANGLES; fa = a - numpy.floor(a)
            f = (factors[h0, a0] * (1 - fa) + factors[h0, a1] * fa) * (1 - fh) + (factors[h1, a0] * (1 - fa) + factors[h1, a1] * fa) * fh
            centre = start + numpy.outer(along, axis)
            parts[n] = centre + (p - centre) * f[:, None]
        remaining.append(sum(int((depth(p) < 0.3).sum()) for p in parts))
        if not remaining[-1]:
            break
    return parts, remaining


def pull(points, faces, depth, passes=20):
    """Moves a shell's points that poke out toward its centre, the move spread over the mesh so the surface stays smooth."""
    centre = points.mean(0)
    neighbours = [[] for _ in range(len(points))]
    for f in faces:
        for i in range(3):
            neighbours[f[i]] += [f[(i + 1) % 3], f[(i + 2) % 3]]
    neighbours = [numpy.unique(n) for n in neighbours]
    for _ in range(passes):
        d = depth(points)
        if (d >= 0.3).all():
            break
        need = numpy.where(d < 0.3, 0.06, 0.0)
        for _ in range(6):
            need = numpy.maximum(need, numpy.array([need[n].mean() if len(n) else 0 for n in neighbours]) * 0.9)
        points = centre + (points - centre) * (1 - need)[:, None]
    return points


def material(label):
    return label if label in ('brain', 'heart', 'gut') else 'teeth' if 'teeth' in label else 'cartilage' if label == 'cartilage' else 'bone'


def turn(axis, degrees):
    a = numpy.radians(degrees)
    c, s = numpy.cos(a), numpy.sin(a)
    x, y, z = numpy.eye(3)[axis]
    k = numpy.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    return numpy.eye(3) + s * k + (1 - c) * (k @ k)


#each scan's axes turned to the horse's, its scale, and where its centre sits, the largest that keeps the brain 0.7 units inside the
#skull and the heart and gut 0.8 units from every bone and the skin, the heart held near its real size between the fifth and
#sixth ribs and left of the midline, and the gut placed after it
ORGANS = [
    #brainstem along the scan's +z, its top along +y, rostral turned down the face
    ('brain', 'head', 'Bip01 Head', turn(0, 73.8), 0.025, (0, 74, 129)),
    #the vessels at the base along the scan's -x turned up, the apex down and back
    ('heart', 'torso', 'Bip01 Spine2', turn(2, -20) @ turn(0, 30) @ numpy.array([[0, -1, 0], [0, 0, 1], [-1, 0, 0]]), 0.095, (-2, 22, 78)),
    ('gut', 'torso', 'Bip01 Spine1', numpy.array([[0, 1, 0], [1, 0, 0], [0, 0, -1]]), 0.10, (0, -10, 80)),
]


def main():
    if 'HAP_HORSE' not in os.environ:
        sys.exit('set HAP_HORSE to the folder holding the HAP horse\'s skeleton.nif and horse_creature.nif')
    ref = Reference(Path(os.environ['HAP_HORSE']))
    vertices, faces = donor()
    parts = [(s, l) for s, l in zip(shells(vertices, faces), LABELS)]
    J = joints(parts)
    trunk = similarity([TRUNK[0], TRUNK[1], TRUNK[0]], [0, TRUNK[2], TRUNK[3]])
    head = head_map(J['skull-C1'])
    #the cervical joints by arc length onto the rig's neck from the placed cervicothoracic joint to the placed condyles
    names = ['C7-T1', 'C6-C7', 'C5-C6', 'C4-C5', 'C3-C4', 'C2-C3', 'C1-C2', 'skull-C1']
    p = numpy.array([J[n] for n in names])
    arc = numpy.r_[0, numpy.cumsum(numpy.linalg.norm(numpy.diff(p, axis=0), axis=1))]
    arc /= arc[-1]
    chain = numpy.array([trunk(J['C7-T1']), ref.joint('Bip01 Neck1'), ref.joint('Bip01 Neck2'), head(J['skull-C1'])])
    span = numpy.r_[0, numpy.cumsum(numpy.linalg.norm(numpy.diff(chain, axis=0), axis=1))]
    q = numpy.array([numpy.array([numpy.interp(f * span[-1], span, chain[:, k]) for k in range(3)]) for f in arc])
    rig = [ref.joint(n) for n in ('Bip01 Neck', 'Bip01 Neck1', 'Bip01 Neck2', 'Bip01 Head')]

    def neck_bone(c):
        best = None
        for k, bone in enumerate(('Bip01 Neck', 'Bip01 Neck1', 'Bip01 Neck2')):
            a, b = rig[k], rig[k + 1]
            t = numpy.clip((c - a) @ (b - a) / ((b - a) @ (b - a)), 0, 1)
            d = numpy.linalg.norm(a + (b - a) * t - c)
            if best is None or d < best[0]:
                best = (d, bone)
        return best[1]
    maps, groups = {}, []
    for side, s in (('left', 'L'), ('right', 'R')):
        g = lambda n: ref.limb(f'Bip01 {s} {n}')
        hip, shoulder = trunk(J[f'{side} hip']), trunk(J[f'{side} shoulder'])
        maps[f'{side} humerus'] = segment(J[f'{side} shoulder'], J[f'{side} elbow'], shoulder, g('UpperArm'))
        maps[f'{side} radius'] = segment(J[f'{side} elbow'], J[f'{side} carpus'], g('UpperArm'), g('Forearm'))
        maps[f'{side} carpus'] = maps[f'{side} front cannon'] = segment(J[f'{side} carpus'], J[f'{side} front fetlock'], g('Forearm'), g('Hand'))
        maps[f'{side} front P1'] = maps[f'{side} front P2'] = segment(J[f'{side} front fetlock'], J[f'{side} front coffin'], g('Hand'), g('Finger0'))
        maps[f'{side} front P3'] = segment(J[f'{side} front fetlock'], J[f'{side} front coffin'], g('Hand'), g('Finger0'),
                                           anchor=(J[f'{side} front coffin'], g('Finger0')))
        maps[f'{side} femur'] = maps[f'{side} patella'] = segment(J[f'{side} hip'], J[f'{side} stifle'], hip, g('Calf'))
        maps[f'{side} tibia'] = maps[f'{side} fibula'] = segment(J[f'{side} stifle'], J[f'{side} hock'], g('Calf'), g('HorseLink'))
        maps[f'{side} tarsus'] = maps[f'{side} hind cannon'] = segment(J[f'{side} hock'], J[f'{side} hind fetlock'], g('HorseLink'), g('Foot'))
        maps[f'{side} hind P1'] = maps[f'{side} hind P2'] = segment(J[f'{side} hind fetlock'], J[f'{side} hind coffin'], g('Foot'), g('Toe0'))
        maps[f'{side} hind P3'] = segment(J[f'{side} hind fetlock'], J[f'{side} hind coffin'], g('Foot'), g('Toe0'),
                                          anchor=(J[f'{side} hind coffin'], g('Toe0')))
        ground = lambda n: g(n) * [1, 1, 0]
        knee = g('Calf')
        groups += [((f'{side} femur', f'{side} patella'), hip, knee + (knee - hip) / numpy.linalg.norm(knee - hip) * 6),
                   ((f'{side} tibia', f'{side} fibula'), knee, g('HorseLink')),
                   ((f'{side} tarsus', f'{side} hind cannon'), g('HorseLink'), g('Foot')),
                   ((f'{side} hind P1', f'{side} hind P2', f'{side} hind P3'), g('Foot'), ground('Toe0')),
                   ((f'{side} humerus',), shoulder, g('UpperArm')), ((f'{side} radius',), g('UpperArm'), g('Forearm')),
                   ((f'{side} carpus', f'{side} front cannon'), g('Forearm'), g('Hand')),
                   ((f'{side} front P1', f'{side} front P2', f'{side} front P3'), g('Hand'), ground('Finger0'))]
    limb_bones = {'humerus': 'Clavicle', 'radius': 'UpperArm', 'carpus': 'Forearm', 'front cannon': 'Forearm', 'front P1': 'Hand',
                  'front P2': 'Hand', 'front P3': 'Hand', 'femur': 'Thigh', 'patella': 'Thigh', 'tibia': 'Calf', 'fibula': 'Calf',
                  'tarsus': 'HorseLink', 'hind cannon': 'HorseLink', 'hind P1': 'Foot', 'hind P2': 'Foot', 'hind P3': 'Foot'}
    spine2 = ref.joint('Bip01 Spine2')[1]
    placed = []
    for shell, label in parts:
        v = shell['v']
        if label in ('skull', 'upper teeth'):
            placed.append((label, head(v), 'head', 'Bip01 Head'))
        elif label in ('mandible', 'lower teeth'):
            placed.append((label, head(v), 'head', 'Bip01 Jaw'))
        elif label[0] == 'C' and label[1:].isdigit():
            n = int(label[1:])
            a, b = names.index(f'C{n}-' + (f'C{n + 1}' if n < 7 else 'T1')), names.index((f'C{n - 1}' if n > 1 else 'skull') + f'-C{n}')
            scale = numpy.linalg.norm(q[b] - q[a]) / numpy.linalg.norm(p[b] - p[a])
            w = segment(p[a], p[b], q[a], q[b], radial=scale)(v)
            placed.append((label, w, 'head', neck_bone(w.mean(0))))
        elif label.startswith('Cd'):
            if int(label[2:]) <= KEPT_TAIL:
                placed.append((label, trunk(v), 'torso', 'Bip01 Pelvis'))
        elif label in ('pelvis', 'sacrum') or label[0] == 'L' and label[1:].isdigit():
            placed.append((label, trunk(v), 'torso', 'Bip01 Pelvis'))
        elif label[0] == 'T' and label[1:].isdigit() or label in ('rib', 'cartilage', 'sternum'):
            w = trunk(v)
            #a rib rides the vertebra its head meets
            top = w[w[:, 2] > w[:, 2].max() - 2].mean(0) if label == 'rib' else w.mean(0)
            placed.append((label, w, 'torso', 'Bip01 Spine2' if top[1] > spine2 or label == 'sternum' else 'Bip01 Spine1'))
        elif label.endswith('scapula'):
            placed.append((label, trunk(v), 'torso', f"Bip01 {'L' if label.startswith('left') else 'R'} Clavicle"))
        else:
            s = 'L' if label.startswith('left') else 'R'
            bone = limb_bones[label.split(' ', 1)[1]]
            region = 'torso' if bone in ('Clavicle', 'Thigh') else 'arms' if bone in ('UpperArm', 'Forearm', 'Hand') else 'legs'
            placed.append((label, maps[label](v), region, f'Bip01 {s} {bone}'))
    kept = [shell for (shell, label) in parts if not (label.startswith('Cd') and int(label[2:]) > KEPT_TAIL)]
    report = {}
    for labels, a, b in groups:
        ids = [n for n, row in enumerate(placed) if row[0] in labels]
        moved, passes = cage([placed[n][1].copy() for n in ids], a, b, ref.depth)
        for n, w in zip(ids, moved):
            placed[n] = (placed[n][0], w) + placed[n][2:]
        report['cage ' + '/'.join(labels)] = passes
    for n, (label, w, region, bone) in enumerate(placed):
        if (ref.depth(w) < 0.3).any():
            placed[n] = (label, pull(w, kept[n]['f'], ref.depth), region, bone)
    outside = {row[0]: int((ref.depth(row[1]) < 0.3).sum()) for row in placed if (ref.depth(row[1]) < 0.3).any()}
    if outside:
        raise RuntimeError('anatomy outside the skin: ' + json.dumps(outside))
    #the organs sit in the room the bones leave inside the skin, the brain in a cavity the build carves into the skull
    occupied = numpy.zeros(ref.shape, bool)
    for (label, w, region, bone), shell in zip(placed, kept):
        solid = ref.voxelise(w, shell['f'])
        occupied |= solid
        if label == 'skull':
            skull = ndimage.distance_transform_edt(solid) * VOXEL
    room = ndimage.distance_transform_edt(ref.inside & ~occupied) * VOXEL
    scans = donors('horse-organ-donors.zip', 'horse-organ-manifest.json')
    for name, region, bone, rotation, scale, centre in ORGANS:
        v, f = scans[name]
        w = ((v - v.mean(0)) @ rotation.T) * scale + numpy.asarray(centre, float)
        margin = ref.sample(skull if name == 'brain' else room, w)
        if (margin < 0.3).any():
            raise RuntimeError(f'{name} touches bone or skin at {int((margin < 0.3).sum())} vertices')
        report[name] = round(float(numpy.median(margin)), 2)
        placed.append((name, w, region, bone))
        #the brainstem's way out of the skull, for the cavity the build carves
        kept.append({'f': f, **({'stem': (rotation @ [0, 0, 1]).tolist()} if name == 'brain' else {})})
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps({'rest': {n: m.tolist() for n, m in ref.rest.items()},
                                  'body': {'vertices': ref.vertices.tolist(), 'triangles': ref.triangles.tolist()},
                                  'shells': [{'label': label, 'material': material(label), 'region': region, 'bone': bone,
                                              'vertices': numpy.round(w, 5).tolist(), 'faces': shell['f'].tolist(),
                                              **({'stem': shell['stem']} if 'stem' in shell else {})}
                                             for (label, w, region, bone), shell in zip(placed, kept)]}))
    print('HORSE_FITTED', len(placed), 'shells', json.dumps(report))


if __name__ == '__main__':
    main()
