"""Shared steps of the Blender anatomy builds: donor checks, mesh cleanup, joint fits and the XRA export."""
import bpy
import bmesh
import json
import math
import struct
import zipfile
import hashlib
import numpy
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parents[1]
RESEARCH=ROOT/'assets/source/extracted'


def extract(*archives):
    #verified donors only, each archive with its own manifest of hashes
    for archive_name,manifest_name in archives:
        with zipfile.ZipFile(ROOT/'assets/source'/archive_name) as archive:
            manifest=json.loads((ROOT/'assets/source'/manifest_name).read_text())
            for item in manifest['files']:
                data=archive.read(item['path'])
                if hashlib.sha256(data).hexdigest()!=item['sha256']:
                    raise RuntimeError('source checksum mismatch: '+item['path'])
                destination=RESEARCH/item['path']
                if not destination.resolve().is_relative_to(RESEARCH.resolve()):
                    raise RuntimeError('source path escapes extraction directory')
                destination.parent.mkdir(parents=True,exist_ok=True)
                destination.write_bytes(data)


def read_obj(path):
    vv,ff=[],[]
    for line in path.read_text().splitlines():
        a=line.split()
        if a and a[0]=='v':
            vv.append(tuple(map(float,a[1:4])))
        elif a and a[0]=='f':
            ff.append(tuple(int(i.split('/')[0])-1 for i in a[1:]))
    return vv,ff


def mesh_object(name,vv,ff,weld_distance=0.0001):
    mesh=bpy.data.meshes.new(name)
    mesh.from_pydata(vv,[],ff)
    obj=bpy.data.objects.new(name,mesh)
    bpy.context.collection.objects.link(obj)
    bm=bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=weld_distance)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    for p in mesh.polygons:
        p.use_smooth=True
    return obj


def apply_modifier(obj,mod):
    bpy.context.view_layer.objects.active=obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def reduce(obj,count):
    tris=sum(len(p.vertices)-2 for p in obj.data.polygons)
    if tris>count:
        mod=obj.modifiers.new('reduction','DECIMATE')
        mod.ratio=count/tris
        apply_modifier(obj,mod)


def remesh(obj,voxel):
    mod=obj.modifiers.new('closed organ','REMESH')
    mod.mode='VOXEL'
    mod.voxel_size=voxel
    mod.adaptivity=0
    apply_modifier(obj,mod)


def keep_largest(obj,fraction):
    #voxel remeshing leaves specks where small vessels poke out, islands well under the largest are dropped
    bm=bmesh.new();bm.from_mesh(obj.data)
    remaining=set(bm.verts);islands=[]
    while remaining:
        seed=remaining.pop();island=[seed];pending=[seed]
        while pending:
            for edge in pending.pop().link_edges:
                for v in edge.verts:
                    if v in remaining:remaining.remove(v);island.append(v);pending.append(v)
        islands.append(island)
    islands.sort(key=len,reverse=True)
    discard=[v for island in islands[1:] if len(island)<fraction*len(islands[0]) for v in island]
    bmesh.ops.delete(bm,geom=discard,context='VERTS')
    bm.to_mesh(obj.data);bm.free();obj.data.update()
    return len(islands),len(discard)


def material(name,color,rough):
    m=bpy.data.materials.new(name)
    m.diffuse_color=(*color,1)
    m.use_nodes=True
    bsdf=m.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value=(*color,1)
    bsdf.inputs['Roughness'].default_value=rough
    return m


def smoothstep(lo,hi,value):
    t=min(1,max(0,(value-lo)/(hi-lo)))
    return t*t*(3-2*t)


def tidy(obj):
    #reduction can leave zero-area faces and loose vertices without a normal
    bm=bmesh.new();bm.from_mesh(obj.data)
    bmesh.ops.dissolve_degenerate(bm,dist=1e-5,edges=list(bm.edges))
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(obj.data);bm.free();obj.data.update()


def open_edges(obj):
    bm=bmesh.new();bm.from_mesh(obj.data);bad=sum(not e.is_manifold for e in bm.edges);bm.free();return bad


def make_materials():
    #material ids match the runtime table
    return [material('bone',(0.60,0.49,0.31),0.48),material('cortex',(0.36,0.105,0.09),0.30),material('bone interior',(0.34,0.15,0.065),0.65),
        material('enamel',(0.85,0.77,0.59),0.25),material('cartilage',(0.72,0.6,0.56),0.35),material('heart',(0.45,0.08,0.07),0.4),
        material('lung',(0.72,0.42,0.42),0.6),material('liver',(0.32,0.07,0.05),0.35),material('gut',(0.75,0.5,0.42),0.45),material('kidney',(0.42,0.12,0.09),0.4),
        material('testis',(0.84,0.74,0.68),0.35),material('penis',(0.55,0.2,0.2),0.4)]


BONE,CORD,CARTILAGE,HEART,LUNG,LIVER,GUT,KIDNEY,TESTIS,PENIS=0,1,4,5,6,7,8,9,10,11


def part_table():
    rows=[line.split('\t') for line in (RESEARCH/'partof_element_parts.txt').read_text().splitlines()[1:]]
    def ids(name):
        found=sorted({r[2] for r in rows if r[1]==name})
        if not found:raise RuntimeError('no donor elements for '+name)
        return found
    return ids


def donor_loader(folder,ids):
    def donor(name):
        vertices=[];faces=[]
        for fid in ids(name):
            vv,ff=read_obj(folder/f'{fid}.obj')
            start=len(vertices);vertices.extend(vv);faces.extend(tuple(start+i for i in f) for f in ff)
        return vertices,faces
    return donor


#joint centres are sphere fits to the femoral and humeral heads
def sphere(points):
    p=numpy.array(points);a=numpy.c_[2*p,numpy.ones(len(p))];b=(p**2).sum(1)
    c=numpy.linalg.lstsq(a,b,rcond=None)[0];return c[:3],math.sqrt(c[3]+(c[:3]**2).sum())
def head_centre(vertices,lean):
    vv=numpy.array(vertices);top=vv[vv[:,2]>vv[:,2].max()-70]
    tip=top[numpy.argmax(top[:,2]-lean*numpy.abs(top[:,0]))]
    cap=vv[numpy.linalg.norm(vv-tip,axis=1)<38];c,r=sphere(cap)
    cap=vv[numpy.abs(numpy.linalg.norm(vv-c,axis=1)-r)<4];c,r=sphere(cap)
    return c


def reference_body(hands=False):
    #the private vanilla body, every shape with its bone weights and binds, in its own skin space
    body=json.loads((ROOT/'assets/source/upperbody.skin.nif.json').read_text())
    for side,letter in ((('left','L'),('right','R')) if hands else ()):
        #the hand files keep their own skin space, carried into the body's through the shared hand bone
        bone=f'Bip01 {letter} Hand'
        own=numpy.array(next(s['bones'][bone]['skin_to_bone'] for s in body if bone in s['bones']))
        for hand in json.loads((ROOT/f'assets/source/{side}hand.skin.nif.json').read_text()):
            to_body=numpy.array(hand['bones'][bone]['skin_to_bone'])@numpy.linalg.inv(own);back=numpy.linalg.inv(to_body)
            body.append({'name':hand['name'],'triangles':hand['triangles'],
                'vertices':[list((numpy.array([*v,1.0])@to_body)[:3]) for v in hand['vertices']],
                'bones':{n:{'skin_to_bone':(back@numpy.array(b['skin_to_bone'])).tolist(),'weights':b['weights']} for n,b in hand['bones'].items()}})
    def rest(name):
        for shape in body:
            if name in shape['bones']:return numpy.linalg.inv(numpy.array(shape['bones'][name]['skin_to_bone']))
        raise RuntimeError('bone missing from reference body: '+name)
    def skin_to_bone(name):
        for shape in body:
            if name in shape['bones']:return numpy.array(shape['bones'][name]['skin_to_bone'])
        raise RuntimeError('bone missing from reference body: '+name)
    return body,rest,skin_to_bone


def to_frame(anatomy,frame):
    for obj in anatomy:
        for v in obj.data.vertices:
            p=numpy.array([*v.co,1.0])@frame;v.co=Vector(p[:3])
        obj.data.update()


def export(anatomy,materials,output,report):
    records=[]
    for obj in anatomy:
        bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        bad=sum(not e.is_manifold for e in bm.edges);bm.to_mesh(obj.data);bm.free();obj.data.update()
        if bad:raise RuntimeError(f'{obj.name}: {bad} nonmanifold edges')
        obj.data.calc_loop_triangles()
        vv=[v.co.copy() for v in obj.data.vertices];ff=[tuple(t.vertices) for t in obj.data.loop_triangles]
        bvh=BVHTree.FromPolygons(vv,ff,all_triangles=True)
        occlusion=[]
        for v in obj.data.vertices:
            n=v.normal.normalized();t=n.cross(Vector((0,0,1)))
            if t.length<0.1:t=n.cross(Vector((0,1,0)))
            t.normalize();b=n.cross(t);blocked=0
            for i in range(8):
                angle=i*2.399963
                d=(n*0.6+(t*math.cos(angle)+b*math.sin(angle))*0.8).normalized()
                blocked+=bvh.ray_cast(v.co+n*0.025,d,1.3)[0] is not None
            occlusion.append(1-0.48*blocked/8)
        pair=float(obj['first']*16+obj['second'])
        material_id=materials.index(obj.data.materials[0])
        verts=[];indices=[];lookup={}
        for tri in obj.data.loop_triangles:
            for index in tri.vertices:
                normal=obj.data.vertices[index].normal
                #a needle-thin tip can average its faces' normals to nothing, its own triangle still faces one way
                if not 0.5<normal.length_squared<1.5:normal=tri.normal
                assert 0.5<normal.length_squared<1.5,(obj.name,index,'invalid normal')
                if index not in lookup:
                    lookup[index]=len(verts);v=vv[index]
                    variation=0.99+0.01*math.sin(v.x*6+math.sin(v.y*7)*2+v.z*8)
                    verts.append((*v,*normal,occlusion[index]*variation,obj['binds'][index],pair))
                indices.append(lookup[index])
        assert len(verts)<65536
        records.append((material_id,3,verts,indices))
        report['meshes'].append({'name':obj.name,'material':material_id,'vertices':len(verts),'triangles':len(indices)//3})
    assert len(records)<=128
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('wb') as f:
        f.write(struct.pack('<4sIII',b'XRA1',3,len(records),0))
        for mat,mode,vv,ii in records:
            f.write(struct.pack('<IIII',mat,mode,len(vv),len(ii)))
            for v in vv:f.write(struct.pack('<9f',*v))
            f.write(struct.pack('<'+'H'*len(ii),*ii))
    report['bytes']=output.stat().st_size
    report['triangles']=sum(len(i)//3 for m,s,v,i in records)
