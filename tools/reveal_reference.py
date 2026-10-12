"""Check the supported aperture topology and prepare a private renderer fixture."""
import collections
import json
import re
import struct
import numpy
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/RevealMask.cpp').read_text()
rings=[list(map(int,ids.split(','))) for ids in re.findall(r'k(?:Mouth|RightEye|LeftEye)\[\]=\{([\d,]+)\}',source)]
assert sorted(map(len,rings))==[14,14,26]
for name in ('headhuman','headfemale'):
    face=json.loads((ROOT/f'assets/source/{name}.nif.json').read_text())[0]
    counts=collections.Counter(tuple(sorted((t[i],t[(i+1)%3]))) for t in face['triangles'] for i in range(3))
    for ring in rings:
        assert all(counts[tuple(sorted((a,ring[(i+1)%len(ring)])))]==1 for i,a in enumerate(ring))
        #the neutral openings are star-shaped in frontal projection, so centroid fans stay inside
        points=[(face['vertices'][i][2],face['vertices'][i][0]) for i in ring]
        cx=sum(p[0] for p in points)/len(points);cy=sum(p[1] for p in points)/len(points)
        crosses=[(a[0]-cx)*(points[(i+1)%len(points)][1]-cy)-(a[1]-cy)*(points[(i+1)%len(points)][0]-cx) for i,a in enumerate(points)]
        assert min(crosses)>0 or max(crosses)<0
    target=ROOT/'tests/output/reveal';target.mkdir(parents=True,exist_ok=True)
    with (target/f'{name}.bin').open('wb') as f:
        for p in face['vertices']:f.write(struct.pack('<3f',*p))
        for t in face['triangles']:f.write(struct.pack('<3H',*t))
    with (target/f'{name}.reveal.bin').open('wb') as f:
        for h,n in zip(face['head_weights'],face['neck_weights']):f.write(struct.pack('<f',min(h+n,1.0)))
    if name=='headhuman':
        with (target/'apertures.bin').open('wb') as f:
            for ring in rings:
                f.write(struct.pack('<3f',*(sum(face['vertices'][j][i] for j in ring)/len(ring) for i in range(3))))
    print('APERTURES_OK',name,'54 boundary edges, 3 star-shaped openings; private fixture exported')
#vanilla torso in rest head space, for replays of captures recorded before the neck skin existed
for name in ('upperbody','femaleupperbody'):
    vertices=[];triangles=[]
    for shape in json.loads((ROOT/f'assets/source/{name}.nif.json').read_text()):
        base=len(vertices)
        vertices+=[(*v,w) for v,w in zip(shape['vertices'],shape['reveal'])]
        triangles+=[[base+i for i in t] for t in shape['triangles']]
    assert len(vertices)<65536
    with (ROOT/f'tests/output/reveal/{name}.bin').open('wb') as f:
        f.write(struct.pack('<2I',len(vertices),len(triangles)))
        for v in vertices:f.write(struct.pack('<4f',*v))
        for t in triangles:f.write(struct.pack('<3H',*t))
    print('TORSO_OK',name,len(vertices),'vertices',len(triangles),'triangles; private fixture exported')
def with_hands(name,shapes):
    #the hand files keep their own skin space, carried into the body's through the shared hand bone
    out=list(shapes)
    for side,letter in (('left','L'),('right','R')):
        bone=f'Bip01 {letter} Hand'
        body=numpy.array(next(s['bones'][bone]['skin_to_bone'] for s in shapes if bone in s['bones']))
        for hand in json.loads((ROOT/f'assets/source/{"female" if name.startswith("female") else ""}{side}hand.skin.nif.json').read_text()):
            to_body=numpy.array(hand['bones'][bone]['skin_to_bone'])@numpy.linalg.inv(body);back=numpy.linalg.inv(to_body)
            out.append({'name':hand['name'],'triangles':hand['triangles'],
                'vertices':[list((numpy.array([*v,1.0])@to_body)[:3]) for v in hand['vertices']],
                'bones':{n:{'skin_to_bone':(back@numpy.array(b['skin_to_bone'])).tolist(),'weights':b['weights']} for n,b in hand['bones'].items()}})
    return out
#the whole vanilla body in a region's rest frame with that region's reveal weight, the frame and skin of a region capture
REGIONS={'torso':('Bip01 Spine1',('Bip01 Pelvis','Bip01 Spine','Bip01 Spine1','Bip01 Spine2','Bip01 L Clavicle','Bip01 R Clavicle')),
    'leftleg':('Bip01 Pelvis',('Bip01 L Thigh','Bip01 L Calf','Bip01 L Foot','Bip01 L Toe0')),
    'rightleg':('Bip01 Pelvis',('Bip01 R Thigh','Bip01 R Calf','Bip01 R Foot','Bip01 R Toe0')),
    'leftarm':('Bip01 Spine2',('Bip01 L UpperArm','Bip01 LUpArmTwistBone','Bip01 L Forearm','Bip01 L ForeTwist','Bip01 L Hand')),
    'rightarm':('Bip01 Spine2',('Bip01 R UpperArm','Bip01 RUpArmTwistBone','Bip01 R Forearm','Bip01 R ForeTwist','Bip01 R Hand'))}
for name,(region,(frame_bone,reveal_bones)) in ((n,r) for n in ('upperbody','femaleupperbody') for r in REGIONS.items()):
    shapes=json.loads((ROOT/f'assets/source/{name}.skin.nif.json').read_text())
    if region.endswith('arm'):shapes=with_hands(name,shapes)
    frame=next(s['bones'][frame_bone]['skin_to_bone'] for s in shapes if frame_bone in s['bones'])
    vertices=[];triangles=[]
    for shape in shapes:
        base=len(vertices)
        for i,v in enumerate(shape['vertices']):
            p=[sum(([*v,1.0])[k]*frame[k][j] for k in range(4)) for j in range(3)]
            w=min(1.0,sum(b['weights'][i] for n,b in shape['bones'].items() if n in reveal_bones))
            vertices.append((*p,w))
        triangles+=[[base+i for i in t] for t in shape['triangles']]
    with (ROOT/f'tests/output/reveal/{name}.{region}.bin').open('wb') as f:
        f.write(struct.pack('<2I',len(vertices),len(triangles)))
        for v in vertices:f.write(struct.pack('<4f',*v))
        for t in triangles:f.write(struct.pack('<3H',*t))
    print('REGION_FRAME_OK',name,region,len(vertices),'vertices',len(triangles),'triangles in rest',frame_bone,'space')
