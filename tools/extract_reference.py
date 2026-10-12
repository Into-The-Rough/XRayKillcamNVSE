"""Extract a private fitting reference from your own Fallout - Meshes.bsa."""
import argparse
import io
import json
import struct
import time
import zlib
from pathlib import Path

if not hasattr(time,'clock'):
    time.clock=time.perf_counter
from pyffi.formats.bsa import BsaFormat
from pyffi.formats.nif import NifFormat

parser=argparse.ArgumentParser()
parser.add_argument('bsa',type=Path)
args=parser.parse_args()
destination=Path(__file__).resolve().parents[1]/'assets/source'
heads={'headhuman.nif':'meshes\\characters\\head','headfemale.nif':'meshes\\characters\\head'}
bodies={'upperbody.nif':('meshes\\characters\\_male','headhuman.nif'),'femaleupperbody.nif':('meshes\\characters\\_male','headfemale.nif')}
hands={n:'meshes\\characters\\_male' for n in ('lefthand.nif','righthand.nif','femalelefthand.nif','femalerighthand.nif')}

def matrix(bone):
    m=bone.skin_transform.get_transform()
    return [[m.m_11,m.m_12,m.m_13,m.m_14],[m.m_21,m.m_22,m.m_23,m.m_24],[m.m_31,m.m_32,m.m_33,m.m_34],[m.m_41,m.m_42,m.m_43,m.m_44]]
def multiply(a,b):
    return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def invert(m):
    #rigid with uniform scale, row-vector convention
    r=[row[:3] for row in m[:3]];s=sum(x*x for x in r[0])
    rt=[[r[j][i]/s for j in range(3)] for i in range(3)]
    t=[-sum(m[3][k]*rt[k][j] for k in range(3)) for j in range(3)]
    return [rt[0]+[0],rt[1]+[0],rt[2]+[0],t+[1]]
def apply(m,v):
    return [v[0]*m[0][j]+v[1]*m[1][j]+v[2]*m[2][j]+m[3][j] for j in range(3)]
def weights(skin,index,count):
    out=[0.0]*count
    for w in skin.data.bone_list[index].vertex_weights:
        out[w.index]=w.weight
    return out
def bone_index(skin,name):
    return next((i for i,b in enumerate(skin.bones) if b and b.name==name.encode()),None)

def read_files(stream,flags,archive,wanted):
    found={}
    for folder in archive.folders:
        directory=(folder.name.decode() if isinstance(folder.name,bytes) else folder.name).lower()
        for entry in folder.files:
            name=(entry.name.decode() if isinstance(entry.name,bytes) else entry.name).lower()
            if wanted.get(name)!=directory:
                continue
            stream.seek(entry.offset);raw=stream.read(entry.file_size.num_bytes)
            if flags&0x100:
                raw=raw[1+raw[0]:]
            if bool(flags&4)^bool(entry.file_size.is_compressed_override):
                raw=zlib.decompress(raw[4:])
            nif=NifFormat.Data();nif.read(io.BytesIO(raw));found[name]=nif
    missing=set(wanted)-set(found)
    if missing:
        raise RuntimeError('files missing: '+str(missing))
    return found

with args.bsa.open('rb') as stream:
    flags=struct.unpack('<4s8I',stream.read(36))[3]
    stream.seek(0)
    archive=BsaFormat.Data();archive.read(stream)
    wanted=dict(heads);wanted.update({k:v[0] for k,v in bodies.items()});wanted.update(hands)
    nifs=read_files(stream,flags,archive,wanted)
destination.mkdir(parents=True,exist_ok=True)
binds={}
for name in heads:
    result=[]
    for shape in nifs[name].get_global_iterator():
        if not isinstance(shape,NifFormat.NiTriBasedGeom) or not shape.skin_instance:
            continue
        skin=shape.skin_instance;head=bone_index(skin,'Bip01 Head');neck=bone_index(skin,'Bip01 Neck1')
        if head is None:
            continue
        transform=skin.data.bone_list[head].skin_transform.get_transform()
        vv=[v*transform for v in shape.data.vertices]
        result.append({'name':shape.name.decode(),'vertices':[[v.x,v.y,v.z] for v in vv],
            'triangles':[list(t) for t in shape.data.get_triangles()],'head_weights':weights(skin,head,len(vv)),
            'neck_weights':weights(skin,neck,len(vv)) if neck is not None else [0.0]*len(vv)})
        binds[name]=(matrix(skin.data.bone_list[head]),matrix(skin.data.bone_list[neck]))
    if not result:
        raise RuntimeError('head geometry missing: '+name)
    (destination/(name+'.json')).write_text(json.dumps(result))
    print(name,len(result[0]['vertices']),'vertices')
#torso skin bound across the neck joint, placed in rest head space through the matching face's neck bone
for name,(folder,face) in bodies.items():
    faceHead,faceNeck=binds[face];result=[]
    for shape in nifs[name].get_global_iterator():
        if not isinstance(shape,NifFormat.NiTriBasedGeom) or not isinstance(shape.skin_instance,NifFormat.BSDismemberSkinInstance):
            continue
        skin=shape.skin_instance;neck=bone_index(skin,'Bip01 Neck1');spine=bone_index(skin,'Bip01 Spine2')
        if neck is None or spine is None or not any(p.part_flag.pf_editor_visible for p in skin.partitions):
            continue
        toHead=multiply(multiply(matrix(skin.data.bone_list[neck]),invert(faceNeck)),faceHead)
        count=len(shape.data.vertices)
        result.append({'name':shape.name.decode(),'vertices':[apply(toHead,[v.x,v.y,v.z]) for v in shape.data.vertices],
            'triangles':[list(t) for t in shape.data.get_triangles()],'reveal':weights(skin,neck,count)})
    (destination/(name+'.json')).write_text(json.dumps(result))
    print(name,[(r['name'],len(r['vertices'])) for r in result])
#the whole body and hands with every bone weight and bind, in their own skin space, for registering and binding anatomy
for name in [*bodies,*hands]:
    shapes=[]
    for shape in nifs[name].get_global_iterator():
        if not isinstance(shape,NifFormat.NiTriBasedGeom) or not isinstance(shape.skin_instance,NifFormat.BSDismemberSkinInstance):
            continue
        skin=shape.skin_instance
        if not any(p.part_flag.pf_editor_visible for p in skin.partitions):
            continue
        count=len(shape.data.vertices)
        shapes.append({'name':shape.name.decode(),'vertices':[[v.x,v.y,v.z] for v in shape.data.vertices],
            'triangles':[list(t) for t in shape.data.get_triangles()],
            'bones':{b.name.decode():{'skin_to_bone':matrix(skin.data.bone_list[i]),'weights':weights(skin,i,count)} for i,b in enumerate(skin.bones)}})
    (destination/(name.replace('.nif','.skin.nif')+'.json')).write_text(json.dumps(shapes))
    print(name,'skin',[(s['name'],len(s['vertices'])) for s in shapes])
