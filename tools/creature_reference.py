"""Export a vanilla creature body as private renderer fixtures, one per region, in the region's rest frame.

Each fixture is the format reveal_reference.py writes: vertex and triangle counts, then per vertex its rest position in
the region's frame bone space and its reveal weight, then the triangles. Beside it go the creature's own rest relations,
frame-local to bone-local as a capture of this bind pose reads them, and its spans as the plugin measures them: each
influence's own length, the region's chain and widths, and the head's crown. The head is captured from the body mesh by
its head, jaw and neck weight, as the plugin does for creatures.

    FNV_DATA=<game Data folder> python tools/creature_reference.py feralghoul
    FNV_DATA=<game Data folder> python tools/creature_reference.py supermutant"""
import struct
import sys
from pathlib import Path

import numpy
from pyffi.formats.nif import NifFormat

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bsa
import human_rest

#each creature's bare body, with the boots and loincloth every super mutant wears over its legs, and its skeleton,
#whose rest pose the binds follow
CREATURES = {
    'feralghoul': (('meshes\\creatures\\ghoul\\feralghoul.nif',), 'meshes\\creatures\\ghoul\\skeleton.nif'),
    'supermutant': (('meshes\\creatures\\smspinebreaker\\nv_sm_v2_base.nif', 'meshes\\creatures\\smspinebreaker\\nv_sm_bootsloin.nif'),
                    'meshes\\creatures\\smspinebreaker\\skeleton.nif'),
}
#frame bone and reveal bones, matching kHeadSpec and kSpecs in src/RegionCapture.cpp
REGIONS = {
    'head': (0, ('Bip01 Head', 'Bip01 Head2', 'Bip01 Neck1')),
    'torso': (1, ('Bip01 Pelvis', 'Bip01 Spine', 'Bip01 Spine1', 'Bip01 Spine2', 'Bip01 L Clavicle', 'Bip01 R Clavicle')),
    'leftleg': (2, ('Bip01 L Thigh', 'Bip01 L Calf', 'Bip01 L Foot', 'Bip01 L Toe0')),
    'leftarm': (3, ('Bip01 L UpperArm', 'Bip01 LUpArmTwistBone', 'Bip01 L Forearm', 'Bip01 L ForeTwist', 'Bip01 L Hand')),
}
#dismemberment caps and gore pieces draw only once a limb is blown off
HIDDEN = ('cap', 'blown')


def read(archive, path, name):
    out = ROOT / 'tests/output/reveal' / name
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(archive.read(path))
    data = NifFormat.Data()
    with out.open('rb') as f:
        data.read(f)
    return data


def bone_names(shape):
    return [bone.name.decode() if bone else '' for bone in shape.skin_instance.bones]


def present(names, name):
    """The skeleton's own name for a bone, Bip01 Neck standing in for Neck1 as FindBone does."""
    if name in names:
        return name
    return 'Bip01 Neck' if name == 'Bip01 Neck1' and 'Bip01 Neck' in names else None


def main(creature):
    body_paths, skeleton_path = CREATURES[creature]
    archive = bsa.Archive(str(human_rest.meshes()))
    rest = human_rest.rest_world(read(archive, skeleton_path, f'{creature}_skeleton.nif'))
    #pyffi holds bone pointers weakly, so every file stays loaded while its shapes are used
    files = [read(archive, path, f'{creature}.nif' if n == 0 else f'{creature}.{n}.nif') for n, path in enumerate(body_paths)]
    shapes = []
    for data in files:
        shapes += [b for root in data.roots for b in root.tree()
                   if isinstance(b, NifFormat.NiTriBasedGeom) and b.skin_instance and not any(h in b.name.decode().lower() for h in HIDDEN)]
    #every bone's rest world in the body's skin space: bound bones from the binds, the rest through the skeleton
    world = {}
    for shape in shapes:
        for name, bone in zip(bone_names(shape), shape.skin_instance.data.bone_list):
            if name:
                world.setdefault(name, numpy.linalg.inv(human_rest.transform(bone.skin_transform)))
    anchor = next(n for n in ('Bip01 Spine1', 'Bip01 Head') if n in world)
    to_skin = world[anchor] @ numpy.linalg.inv(rest[anchor])
    for name, m in rest.items():
        world.setdefault(name, to_skin @ m)
    #each shape's vertices in that skin space, through its first bone
    placed = []
    for shape in shapes:
        names = bone_names(shape)
        j = next(i for i, n in enumerate(names) if n)
        m = world[names[j]] @ human_rest.transform(shape.skin_instance.data.bone_list[j].skin_transform)
        placed.append((shape, names, numpy.array([(m @ [v.x, v.y, v.z, 1.0])[:3] for v in shape.data.vertices])))
    for region, (index, reveal_bones) in REGIONS.items():
        influences = [present(world, b) if b else None for b in human_rest.REGIONS[index]]
        frame = world[influences[0]]
        local = numpy.linalg.inv(frame)
        vertices, triangles = [], []
        for shape, names, points in placed:
            reveal = numpy.zeros(len(points))
            for name, bone in zip(names, shape.skin_instance.data.bone_list):
                if name in reveal_bones or present(names, name) in [present(names, r) for r in reveal_bones]:
                    for w in bone.vertex_weights:
                        reveal[w.index] += w.weight
            base = len(vertices)
            for p, r in zip(points, reveal):
                q = local @ numpy.array([*p, 1.0])
                vertices.append((q[0], q[1], q[2], min(float(r), 1.0)))
            triangles += [(base + t.v_1, base + t.v_2, base + t.v_3) for t in shape.data.triangles]
        assert len(vertices) < 65536
        out = ROOT / f'tests/output/reveal/{creature}.{region}'
        with open(f'{out}.bin', 'wb') as f:
            f.write(struct.pack('<2I', len(vertices), len(triangles)))
            for v in vertices:
                f.write(struct.pack('<4f', *v))
            for t in triangles:
                f.write(struct.pack('<3H', *t))
        #frame-local to bone-local, as a capture of this bind pose reads them from the skin
        with open(f'{out}.rest.bin', 'wb') as f:
            for k in range(7):
                m = numpy.linalg.inv(world[influences[k]]) @ frame if influences[k] else numpy.eye(4)
                scale = numpy.cbrt(numpy.linalg.det(m[:3, :3]))
                f.write(struct.pack('<13f', *(m[:3, :3] / scale).flatten(), *m[:3, 3], scale))
        #the spans the plugin measures on the live skeleton, here at rest, and the crown from the shapes bound across the neck
        distance = lambda a, b: float(numpy.linalg.norm(world[b][:3, 3] - world[a][:3, 3])) if a in world and b in world else 0.0
        reach = [distance(influences[k], r) if r and influences[k] else 0.0 for k, r in enumerate(human_rest.REACH[index])]
        chain_bones = human_rest.CHAIN[index]
        chain = sum(distance(chain_bones[i], chain_bones[i + 1]) for i in range(len(chain_bones) - 1)) if all(b in world for b in chain_bones) else 0.0
        widths = [distance(a, b) for a, b in human_rest.WIDTHS[index]] + [0.0] * (2 - len(human_rest.WIDTHS[index]))
        crown = 0.0
        if index == 0:
            for shape, names, points in placed:
                neck = influences[1]
                if 'Bip01 Head' not in names or neck not in names:
                    continue
                bone = shape.skin_instance.data.bone_list[names.index('Bip01 Head')]
                head = numpy.linalg.inv(world['Bip01 Head'])
                for w in bone.vertex_weights:
                    if w.weight >= .5:
                        crown = max(crown, (head @ [*points[w.index], 1.0])[0])
        with open(f'{out}.spans.bin', 'wb') as f:
            f.write(struct.pack('<11f', *reach, chain, *widths, crown))
        opened = sum(1 for v in vertices if v[3] > .3)
        print(f'{creature.upper()}_OK', region, len(vertices), 'vertices', len(triangles), 'triangles', opened, 'open', 'chain', round(chain, 2),
              'widths', [round(w, 2) for w in widths], 'crown', round(crown, 2), 'reach', [round(r, 2) for r in reach], '; private fixture exported')


if __name__ == '__main__':
    for name in sys.argv[1:] or CREATURES:
        main(name)
