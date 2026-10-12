"""Export the HAP horse body as private renderer fixtures, one per horse region, in the region's rest frame.

Each fixture is the format creature_reference.py writes: vertex and triangle counts, then per vertex its rest position in the
region's frame bone space and its reveal weight, then the triangles, and beside it the horse's own rest relations.

    HAP_HORSE=<folder with skeleton.nif and horse_creature.nif> python tools/horse_reference.py"""
import os
import struct
import sys
from pathlib import Path

import numpy
from pyffi.formats.nif import NifFormat

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import human_rest
import horse_rest

#frame bone index into horse_rest.REGIONS and the reveal bones, matching kSpecs in src/RegionCapture.cpp
REGIONS = {
    'head': (0, ('Bip01 Head', 'Bip01 Jaw', 'Bip01 Ear L', 'Bip01 Ear R', 'Bip01 Neck2', 'Bip01 Neck1', 'Bip01 Neck')),
    'torso': (1, ('Bip01 Pelvis', 'Bip01 Spine', 'Bip01 Spine1', 'Bip01 Spine2', 'Bip01 L Clavicle', 'Bip01 R Clavicle', 'Bip01 L Thigh', 'Bip01 R Thigh')),
    'leftleg': (2, ('Bip01 L Calf', 'Bip01 L HorseLink', 'Bip01 L Foot', 'Bip01 L Toe0')),
    'leftarm': (3, ('Bip01 L UpperArm', 'Bip01 L Forearm', 'Bip01 L Hand', 'Bip01 L Finger0')),
}


def main():
    if 'HAP_HORSE' not in os.environ:
        sys.exit('set HAP_HORSE to the folder holding the HAP horse\'s skeleton.nif and horse_creature.nif')
    folder = Path(os.environ['HAP_HORSE'])
    skeleton = NifFormat.Data()
    with open(folder / 'skeleton.nif', 'rb') as f:
        skeleton.read(f)
    rest = human_rest.rest_world(skeleton)
    body = NifFormat.Data()
    with open(folder / 'horse_creature.nif', 'rb') as f:
        body.read(f)
    #the body alone, the saddle, mane and bridle add skin the planning routes never need
    shape = next(s for s in body.get_global_iterator() if isinstance(s, NifFormat.NiTriBasedGeom) and s.name.decode() == 'Horse Body')
    names = [b.name.decode() for b in shape.skin_instance.bones]
    points = numpy.array([[p.x, p.y, p.z] for p in shape.data.vertices])
    triangles = [(t.v_1, t.v_2, t.v_3) for t in shape.data.triangles]
    out = ROOT / 'tests/output/reveal'
    out.mkdir(parents=True, exist_ok=True)
    for region, (index, reveal_bones) in REGIONS.items():
        influences = horse_rest.REGIONS[index]
        frame = rest[influences[0]]
        local = numpy.linalg.inv(frame)
        reveal = numpy.zeros(len(points))
        for name, bone in zip(names, shape.skin_instance.data.bone_list):
            if name in reveal_bones:
                for w in bone.vertex_weights:
                    reveal[w.index] += w.weight
        with open(out / f'horse.{region}.bin', 'wb') as f:
            f.write(struct.pack('<2I', len(points), len(triangles)))
            for p, r in zip(points, reveal):
                q = local @ numpy.array([*p, 1.0])
                f.write(struct.pack('<4f', q[0], q[1], q[2], min(float(r), 1.0)))
            for t in triangles:
                f.write(struct.pack('<3H', *t))
        with open(out / f'horse.{region}.rest.bin', 'wb') as f:
            for bone in influences:
                m = numpy.linalg.inv(rest[bone]) @ frame if bone else numpy.eye(4)
                scale = numpy.cbrt(numpy.linalg.det(m[:3, :3]))
                f.write(struct.pack('<13f', *(m[:3, :3] / scale).flatten(), *m[:3, 3], scale))
        print('HORSE_REFERENCE_OK', region, len(points), 'vertices', len(triangles), 'triangles', int((reveal > .3).sum()), 'open; private fixture exported')


if __name__ == '__main__':
    main()
