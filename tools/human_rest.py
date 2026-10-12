"""Generate src/HumanRest.h: each region's influence bones relative to its frame bone in the vanilla male skeleton's rest pose,
and the human spans a creature's proportions are measured against.

The anatomy is built on the vanilla male body, whose binds match this rest pose. A creature's region capture places each
bone's anatomy through these relations, so the anatomy keeps its human placement on every bone whatever the creature's
own bind pose, scaled by the creature's spans over these."""
import os
import sys
from pathlib import Path

import numpy
from pyffi.formats.nif import NifFormat

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bsa


def meshes():
    """The game's Fallout - Meshes.bsa, from the Data folder FNV_DATA names."""
    if 'FNV_DATA' not in os.environ:
        sys.exit('set FNV_DATA to the game\'s Data folder')
    return Path(os.environ['FNV_DATA']) / 'Fallout - Meshes.bsa'


#the influence order of kHeadSpec and kSpecs in src/RegionCapture.cpp, frame bone first, None where a region names none
REGIONS = [
    ['Bip01 Head', 'Bip01 Neck1', None, None, None, None, None],
    ['Bip01 Spine1', 'Bip01 Spine2', 'Bip01 Spine', 'Bip01 Pelvis', 'Bip01 Neck1', 'Bip01 L Clavicle', 'Bip01 R Clavicle'],
    ['Bip01 Pelvis', 'Bip01 L Thigh', 'Bip01 L Calf', 'Bip01 L Foot', 'Bip01 R Thigh', 'Bip01 R Calf', 'Bip01 R Foot'],
    ['Bip01 Spine2', 'Bip01 L UpperArm', 'Bip01 L Forearm', 'Bip01 L Hand', 'Bip01 R UpperArm', 'Bip01 R Forearm', 'Bip01 R Hand'],
]
#the bone each influence's own length runs to, None where the region's chain or crown sets it
REACH = [
    [None] * 7,
    [None] * 7,
    [None, 'Bip01 L Calf', 'Bip01 L Foot', 'Bip01 L Toe0', 'Bip01 R Calf', 'Bip01 R Foot', 'Bip01 R Toe0'],
    [None, 'Bip01 L Forearm', 'Bip01 L Hand', 'Bip01 L Finger2', 'Bip01 R Forearm', 'Bip01 R Hand', 'Bip01 R Finger2'],
]
#the bones whose links sum to a region's length, and the pairs whose separations set its girth
CHAIN = [[], ['Bip01 Pelvis', 'Bip01 Spine', 'Bip01 Spine1', 'Bip01 Spine2', 'Bip01 Neck'], [], []]
WIDTHS = [[], [('Bip01 L UpperArm', 'Bip01 R UpperArm'), ('Bip01 L Thigh', 'Bip01 R Thigh')], [], []]
CHAIN_BONES = 5


def transform(t):
    rows = [[t.rotation.m_11, t.rotation.m_12, t.rotation.m_13], [t.rotation.m_21, t.rotation.m_22, t.rotation.m_23],
            [t.rotation.m_31, t.rotation.m_32, t.rotation.m_33]]
    m = numpy.eye(4)
    m[:3, :3] = numpy.array(rows).T * t.scale
    m[:3, 3] = [t.translation.x, t.translation.y, t.translation.z]
    return m


def rest_world(data):
    out = {}

    def walk(node, parent):
        if not isinstance(node, NifFormat.NiAVObject):
            return
        world = parent @ transform(node)
        out[node.name.decode()] = world
        if isinstance(node, NifFormat.NiNode):
            for child in node.children:
                if child:
                    walk(child, world)
    for root in data.roots:
        walk(root, numpy.eye(4))
    return out


def number(v):
    text = f'{v if abs(v) > 1e-7 else 0.0:.7g}'
    return (text if '.' in text or 'e' in text else text + '.0') + 'f'


def literal(m):
    scale = numpy.cbrt(numpy.linalg.det(m[:3, :3]))
    r = m[:3, :3] / scale
    values = [*r.flatten(), *m[:3, 3]]
    return '{{' + ','.join(map(number, values[:9])) + '},{' + ','.join(map(number, values[9:])) + '},' + number(scale) + '}'


def name(n):
    return f'"{n}"' if n else 'nullptr'


def crown(archive):
    """The vanilla male head's highest head-weighted vertex above Bip01 Head, in the shape bound across the neck."""
    path = ROOT / 'tests/output/reveal/headhuman.nif'
    path.write_bytes(archive.read('meshes\\characters\\head\\headhuman.nif'))
    data = NifFormat.Data()
    with path.open('rb') as f:
        data.read(f)
    best = 0.0
    for root in data.roots:
        for shape in root.tree():
            if not isinstance(shape, NifFormat.NiTriBasedGeom) or not shape.skin_instance:
                continue
            names = [b.name.decode() if b else '' for b in shape.skin_instance.bones]
            if 'Bip01 Head' not in names or not any(n.startswith('Bip01 Neck') for n in names):
                continue
            bone = shape.skin_instance.data.bone_list[names.index('Bip01 Head')]
            m = transform(bone.skin_transform)
            for w in bone.vertex_weights:
                if w.weight >= .5:
                    v = shape.data.vertices[w.index]
                    best = max(best, (m @ numpy.array([v.x, v.y, v.z, 1.0]))[0])
    return best


def main():
    archive = bsa.Archive(str(meshes()))
    path = ROOT / 'tests/output/reveal/human_skeleton.nif'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(archive.read('meshes\\characters\\_male\\skeleton.nif'))
    data = NifFormat.Data()
    with path.open('rb') as f:
        data.read(f)
    world = rest_world(data)
    rows = []
    for bones in REGIONS:
        frame = world[bones[0]]
        #frame-local to bone-local: the relation a capture reads from skin binds, skinToBone(bone) * inverse(skinToBone(frame))
        rows.append('{' + ','.join(literal(numpy.linalg.inv(world[b]) @ frame) if b else literal(numpy.eye(4)) for b in bones) + '}')
    distance = lambda a, b: float(numpy.linalg.norm(world[b][:3, 3] - world[a][:3, 3]))
    reach = ['{' + ','.join(number(distance(bones[k], r)) if r else '0.0f' for k, r in enumerate(names)) + '}' for bones, names in zip(REGIONS, REACH)]
    chain = [number(sum(distance(c[i], c[i + 1]) for i in range(len(c) - 1))) if c else '0.0f' for c in CHAIN]
    widths = ['{' + ','.join(number(distance(a, b)) for a, b in w) + '}' if w else '{0.0f,0.0f}' for w in WIDTHS]
    pad = lambda items, n: list(items) + [None] * (n - len(items))
    header = ['#pragma once', '//generated by tools/human_rest.py from the vanilla male skeleton and head', '#include "Math.h"', '#include "Model.h"', '',
              'namespace XRay {', '//each region\'s influences in kHeadSpec and kSpecs order, frame-local to bone-local in the human rest pose',
              'static const Transform kHumanRest[4][kInfluences]={', ',\n'.join('    ' + r for r in rows), '};',
              '//the bone each influence\'s own length runs to, nullptr where the region\'s chain or crown sets it',
              'static const char* const kReachBones[4][kInfluences]={', ',\n'.join('    {' + ','.join(map(name, r)) + '}' for r in REACH), '};',
              '//the bones whose links sum to each region\'s length, and the pairs whose separations set its girth',
              f'constexpr unsigned kChainBones={CHAIN_BONES};',
              'static const char* const kChainBoneNames[4][kChainBones]={', ',\n'.join('    {' + ','.join(map(name, pad(c, CHAIN_BONES))) + '}' for c in CHAIN), '};',
              'static const char* const kWidthBones[4][2][2]={',
              ',\n'.join('    {' + ','.join('{' + ','.join(map(name, p)) + '}' for p in pad(w, 2)) + '}' if w else '    {{nullptr,nullptr},{nullptr,nullptr}}' for w in WIDTHS), '};',
              '//those spans in the human rest pose, 0 where none is named',
              'static const float kHumanReach[4][kInfluences]={', ',\n'.join('    ' + r for r in reach), '};',
              'static const float kHumanChain[4]={' + ','.join(chain) + '};',
              'static const float kHumanWidths[4][2]={' + ','.join(widths) + '};',
              '//the vanilla male head\'s crown above Bip01 Head along the head\'s x axis, in the shape bound across the neck',
              f'constexpr float kHumanCrown={number(crown(archive))};', '}', '']
    (ROOT / 'src/HumanRest.h').write_text('\n'.join(header))
    print('HUMAN_REST_OK', len(rows), 'regions')


if __name__ == '__main__':
    main()
