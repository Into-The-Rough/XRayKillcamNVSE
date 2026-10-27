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

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT/'assets/source/extracted'
with zipfile.ZipFile(ROOT/'assets/source/donors.zip') as archive:
    manifest=json.loads((ROOT/'assets/source/manifest.json').read_text())
    for item in manifest['files']:
        data=archive.read(item['path'])
        if hashlib.sha256(data).hexdigest()!=item['sha256']:
            raise RuntimeError('source checksum mismatch: '+item['path'])
        destination=RESEARCH/item['path']
        if not destination.resolve().is_relative_to(RESEARCH.resolve()):
            raise RuntimeError('source path escapes extraction directory')
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_bytes(data)
OUTPUT = ROOT/'mod/meshes/XRayKillcamNVSE/head.xra'
bpy.ops.wm.read_factory_settings(use_empty=True)
report = {'coordinate_space':'Bip01 Head local; X up, Y forward, Z lateral', 'meshes':[]}


def read_obj(path):
    vv, ff = [], []
    for line in path.read_text().splitlines():
        a = line.split()
        if a and a[0] == 'v':
            vv.append(tuple(map(float,a[1:4])))
        elif a and a[0] == 'f':
            ff.append(tuple(int(i.split('/')[0])-1 for i in a[1:]))
    return vv, ff


def mesh_object(name, vv, ff, weld_distance=0.0001):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vv,[],ff)
    obj = bpy.data.objects.new(name,mesh)
    bpy.context.collection.objects.link(obj)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=weld_distance)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    for p in mesh.polygons:
        p.use_smooth = True
    return obj


def modifier(obj, mod):
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def reduce(obj, count):
    tris = sum(len(p.vertices)-2 for p in obj.data.polygons)
    if tris > count:
        mod = obj.modifiers.new('reduction','DECIMATE')
        mod.ratio = count/tris
        modifier(obj,mod)


def bone_fit(v):
    return ((v[2]-1421.63)*(14.7/214.64)-0.55, -(v[1]+86)*0.064+0.75, v[0]*0.071)


def smooth(lo,hi,value):
    t=min(1,max(0,(value-lo)/(hi-lo)))
    return t*t*(3-2*t)


def material(name,color,rough):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color,1)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (*color,1)
    bsdf.inputs['Roughness'].default_value = rough
    return m


materials = [material('bone',(0.60,0.49,0.31),0.48),material('cortex',(0.36,0.105,0.09),0.30),material('bone interior',(0.34,0.15,0.065),0.65),material('enamel',(0.85,0.77,0.59),0.25),material('cartilage',(0.72,0.6,0.56),0.35)]
skull = []
for p in sorted((RESEARCH/'bodyparts3d/skull').glob('*.obj')):
    if p.stem < 'FJ3199' or p.stem in ('FJ3199','FJ3201'):
        continue
    vv,ff = read_obj(p)
    obj = mesh_object(p.stem,[bone_fit(v) for v in vv],ff)
    reduce(obj,max(700,int(len(ff)*0.45)))
    obj.data.materials.append(materials[0])
    obj.data.materials.append(materials[2])
    skull.append(obj)

teeth=[]
tooth_rows=[line.split('\t') for line in (RESEARCH/'isa_element_parts.txt').read_text().splitlines()[1:]]
report['dentition']={}
for arch in ('upper','lower'):
    ids=sorted({r[2] for r in tooth_rows if r[1].startswith(('left '+arch,'right '+arch)) and r[1].endswith('tooth')})
    assert len(ids)==14
    vertices=[];faces=[]
    for fid in ids:
        vv,ff=read_obj(RESEARCH/'bodyparts3d/teeth'/f'{fid}.obj')
        start=len(vertices);vertices.extend(bone_fit(v) for v in vv)
        faces.extend(tuple(start+i for i in f) for f in ff)
    #the skull weld tolerance collapses a small molar edge in fj1275
    obj=mesh_object(arch+'_teeth',vertices,faces,weld_distance=0.000001)
    reduce(obj,12000)
    bm=bmesh.new();bm.from_mesh(obj.data)
    remaining=set(bm.verts);discard=[];components=[]
    while remaining:
        seed=remaining.pop();component=[seed];pending=[seed]
        while pending:
            for edge in pending.pop().link_edges:
                for v in edge.verts:
                    if v in remaining:remaining.remove(v);component.append(v);pending.append(v)
        #discard tiny disconnected donor/decimation fragments, each retained tooth has hundreds of vertices
        if len(component)<16:discard.extend(component)
        else:components.append(len(component))
    bmesh.ops.delete(bm,geom=discard,context='VERTS')
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free();obj.data.update()
    assert len(components)==14,(arch,components)
    obj.data.materials.append(materials[3]);teeth.append(obj)
    obj['source_teeth']=','.join(ids)
    report['dentition'][arch]=ids
    report['dentition'][arch+'_discarded_degenerate_vertices']=len(discard)

rows = [line.split('\t') for line in (RESEARCH/'partof_element_parts.txt').read_text().splitlines()[1:]]
ids = {r[2] for r in rows if r[1] in ('left cerebral hemisphere','right cerebral hemisphere')}
dest = [v for fid in ids for v in read_obj(RESEARCH/'bodyparts3d/brain'/f'{fid}.obj')[0]]
lo = Vector(tuple(min(v[i] for v in dest) for i in range(3)))
hi = Vector(tuple(max(v[i] for v in dest) for i in range(3)))
hemis = []
for path in sorted((RESEARCH/'brainder').glob('*.obj')):
    vv,ff = read_obj(path)
    hemis.append((path.stem,[(-v[0],-v[1],v[2]) for v in vv],ff))
points = [v for _,vv,_ in hemis for v in vv]
slo = Vector(tuple(min(v[i] for v in points) for i in range(3)))
shi = Vector(tuple(max(v[i] for v in points) for i in range(3)))
brain = []
for name,vv,ff in hemis:
    fitted = [bone_fit(tuple((v[i]-slo[i])*(hi[i]-lo[i])/(shi[i]-slo[i])+lo[i] for i in range(3))) for v in vv]
    obj = mesh_object(name,fitted,ff)
    reduce(obj,32000)
    obj.data.materials.append(materials[1])
    brain.append(obj)
for path in sorted((RESEARCH/'bodyparts3d/cerebellum').glob('*.obj')):
    vv,ff = read_obj(path)
    obj = mesh_object('cerebellum_'+path.stem,[bone_fit(v) for v in vv],ff)
    reduce(obj,4000)
    obj.data.materials.append(materials[1])
    brain.append(obj)

#cervical spine, hyoid and airway from the same donor body, so the skull registration places them consistently
NECK_PARTS=(('C1','FJ3176',0,1400),('C2','FJ3177',0,1400),('C3','FJ3161',0,1400),('C4','FJ3164',0,1400),('C5','FJ3167',0,1400),
    ('C6','FJ3170',0,1400),('C7','FJ3172',0,1400),('hyoid','FJ2772',0,1200),('thyroid cartilage','FJ2808',4,1600),
    ('cricoid cartilage','FJ2440',4,1200),('trachea','FJ2541',4,2400))
neck=[]
for name,fid,slot,budget in NECK_PARTS:
    vv,ff=read_obj(RESEARCH/'bodyparts3d/neck'/f'{fid}.obj')
    obj=mesh_object('neck '+name,[bone_fit(v) for v in vv],ff)
    reduce(obj,budget)
    obj.data.materials.append(materials[slot])
    obj['source']=fid
    neck.append(obj)
vertebrae=neck[:7]

reference = json.loads((ROOT/'assets/source/headhuman.nif.json').read_text())[0]
head_vertices=[Vector(v) for v in reference['vertices']]
target_eyes=[(head_vertices[a]+head_vertices[b])*0.5 for a,b in ((428,624),(476,866))]
source_eyes=[]
for fid in ('FJ1317','FJ1368'):
    vv,_=read_obj(RESEARCH/'bodyparts3d/eyes'/f'{fid}.obj')
    #the right-sclera donor contains tiny islands across the midline
    side=1 if fid=='FJ1317' else -1
    points=[Vector(bone_fit(v)) for v in vv if v[0]*side>0]
    lo=Vector(tuple(min(v[i] for v in points) for i in range(3)))
    hi=Vector(tuple(max(v[i] for v in points) for i in range(3)))
    source_eyes.append(Vector(((lo.x+hi.x)*0.5,hi.y,(lo.z+hi.z)*0.5)))
source_eye=(source_eyes[0]+source_eyes[1])*0.5
target_eye=(target_eyes[0]+target_eyes[1])*0.5
chin=sum((head_vertices[i] for i in (35,187,27)),Vector())/3
mandible=next(o for o in skull if o.name=='FJ3289')
source_chin=min(v.co.x for v in mandible.data.vertices)
target_chin=chin.x+0.2
source_top=max(v.co.x for o in skull for v in o.data.vertices)
target_top=head_vertices[149].x-0.55
chin_forward=chin.y-0.35-max(v.co.y for v in mandible.data.vertices)
eye_forward=target_eye.y-source_eye.y
eye_width=abs(target_eyes[0].z-target_eyes[1].z)/abs(source_eyes[0].z-source_eyes[1].z)
assert 0.8<eye_width<1.25


def register_landmarks(point):
    x,y,z=point
    lower=target_chin+(x-source_chin)*(target_eye.x-target_chin)/(source_eye.x-source_chin)
    upper=target_eye.x+(x-source_eye.x)*(target_top-target_eye.x)/(source_top-source_eye.x)
    t=smooth(source_eye.x-0.75,source_eye.x+0.75,x)
    height=lower*(1-t)+upper*t
    if x<source_eye.x:
        forward=chin_forward+(eye_forward-chin_forward)*smooth(source_chin,source_eye.x,x)
    else:
        forward=eye_forward*(1-smooth(source_eye.x,source_top,x))
    front=smooth(0,4,y)
    width=1+(eye_width-1)*front*math.exp(-((x-source_eye.x)/4)**2)
    return Vector((height,y+forward*front,z*width))


for obj in skull+brain+teeth+neck:
    for v in obj.data.vertices:v.co=register_landmarks(v.co)
    obj.data.update()
report['registration']={
    'source_eye_front':list(source_eye),'target_eye_corners':list(target_eye),
    'eye_height_correction':target_eye.x-source_eye.x,'eye_forward_correction':eye_forward,
    'chin_forward_correction':chin_forward,'eye_width_scale':eye_width,
    'source_chin_height':source_chin,'target_chin_height':target_chin,
    'source_crown_height':source_top,'target_crown_height':target_top,
}
ref = mesh_object('vanilla male reference',reference['vertices'],reference['triangles'])
ref.hide_render = True
ref.display_type = 'WIRE'
ref.show_in_front = True
head_bvh = BVHTree.FromPolygons([Vector(v) for v in reference['vertices']],reference['triangles'],all_triangles=True)

#the radial cage is shared by the skull and brain, with skull samples setting its displacement
radii = [[100.0]*32 for _ in range(18)]
for level in range(18):
    height = 1.0+level*0.75
    origin = Vector((height,0.5,0))
    for a in range(32):
        theta = a*math.tau/32
        direction = Vector((0,math.cos(theta),math.sin(theta)))
        point,normal,index,distance = head_bvh.ray_cast(origin,direction,30)
        if point is not None:
            radii[level][a] = max(0.4,distance-0.4)
factors = [[1.0]*32 for _ in range(18)]
for obj in skull:
    for v in obj.data.vertices:
        h,y,z = v.co
        if h < 2.0 or h > 13.6:
            continue
        level = min(17,max(0,round((h-1)/0.75)))
        a = round((math.atan2(z,y-0.5)%math.tau)*32/math.tau)%32
        r = math.hypot(y-0.5,z)
        if r > 0.01:
            factors[level][a] = min(factors[level][a],radii[level][a]/r)
for _ in range(2):
    factors = [[sum(factors[max(0,min(17,h+dh))][(a+da)%32] for dh,da in ((0,0),(-1,0),(1,0),(0,-1),(0,1)))/5 for a in range(32)] for h in range(18)]
for obj in skull+brain+teeth+neck:
    for v in obj.data.vertices:
        h,y,z = v.co
        if h < 2.0 or h > 13.6:
            continue
        ht = min(17,max(0,(h-1)/0.75));h0=int(ht);h1=min(17,h0+1)
        at = (math.atan2(z,y-0.5)%math.tau)*32/math.tau;a0=int(at)%32;a1=(a0+1)%32
        f0 = factors[h0][a0]*(1-(at-int(at)))+factors[h0][a1]*(at-int(at))
        f1 = factors[h1][a0]*(1-(at-int(at)))+factors[h1][a1]*(at-int(at))
        f = f0*(1-(ht-h0))+f1*(ht-h0)
        v.co.y = 0.5+(y-0.5)*f
        v.co.z = z*f
    obj.data.update()

for obj in brain:
    center=register_landmarks(Vector((8.5,0,0)))
    for v in obj.data.vertices:
        v.co=center+(v.co-center)*0.94
    obj.data.update()

#bodyparts3d has no cervical cord, so a tube follows the measured vertebral canal up into the brainstem
spine_vertices=[];spine_faces=[]
for obj in vertebrae:
    obj.data.calc_loop_triangles();start=len(spine_vertices)
    spine_vertices.extend(v.co.copy() for v in obj.data.vertices)
    spine_faces.extend(tuple(start+i for i in t.vertices) for t in obj.data.loop_triangles)
spine_bvh=BVHTree.FromPolygons(spine_vertices,spine_faces,all_triangles=True)
canal=[]
for obj in vertebrae:
    lo=min(v.co.x for v in obj.data.vertices);hi=max(v.co.x for v in obj.data.vertices)
    for fraction in (0.35,0.5,0.65):
        height=lo+(hi-lo)*fraction;hits=[];origin=Vector((height,8,0))
        while len(hits)<16:
            point,_,_,_=spine_bvh.ray_cast(origin,Vector((0,-1,0)),20)
            if point is None:break
            hits.append(point.y);origin=Vector((height,point.y-1e-4,0))
        gaps=[(hits[k]-hits[k+1],(hits[k]+hits[k+1])*0.5) for k in range(1,len(hits)-1,2)]
        gaps=[g for g in gaps if 0.45<g[0]<3.2]
        if gaps:canal.append(Vector((height,max(gaps)[1],0)))
canal.sort(key=lambda p:p.x)
#a ray through a foramen or between processes can pick the wrong gap, the neighbours' median rejects it
canal=[p for i,p in enumerate(canal) if abs(p.y-sorted(q.y for q in canal[max(0,i-2):i+3])[len(canal[max(0,i-2):i+3])//2])<0.45]
assert len(canal)>=14,len(canal)
smoothed=[sum(canal[max(0,i-1):i+2],Vector())/len(canal[max(0,i-1):i+2]) for i in range(len(canal))]
rise=(smoothed[-1]-smoothed[-3]).normalized();fall=(smoothed[0]-smoothed[2]).normalized()
path=[smoothed[0]+fall*1.2]+smoothed+[smoothed[-1]+rise*0.9,smoothed[-1]+rise*1.8]
samples=[path[0]]
for a,b in zip(path,path[1:]):
    steps=max(1,int((b-a).length/0.22))
    samples.extend(a+(b-a)*(i/steps) for i in range(1,steps+1))
rings=[];cord_vertices=[];cord_faces=[];sides=16
for i,p in enumerate(samples):
    tangent=(samples[min(i+1,len(samples)-1)]-samples[max(i-1,0)]).normalized()
    lateral=Vector((0,0,1));depth=tangent.cross(lateral).normalized()
    arc=min(i,len(samples)-1-i)*0.22
    taper=math.sqrt(max(0,min(1,(arc+0.05)/0.5)))
    ring=[]
    for s in range(sides):
        a=s*math.tau/sides
        ring.append(len(cord_vertices));cord_vertices.append(p+lateral*(math.cos(a)*0.40*taper)+depth*(math.sin(a)*0.27*taper))
    rings.append(ring)
for r0,r1 in zip(rings,rings[1:]):
    for s in range(sides):
        cord_faces.append((r0[s],r0[(s+1)%sides],r1[(s+1)%sides],r1[s]))
for ring,pole,flip in ((rings[0],samples[0]-(samples[1]-samples[0]),True),(rings[-1],samples[-1]+(samples[-1]-samples[-2]),False)):
    index=len(cord_vertices);cord_vertices.append(pole)
    for s in range(sides):
        a,b=ring[s],ring[(s+1)%sides]
        cord_faces.append((index,a,b) if flip else (index,b,a))
cord=mesh_object('neck spinal cord',[tuple(v) for v in cord_vertices],cord_faces)
cord.data.materials.append(materials[1])
neck.append(cord)
report['neck']={'canal':[list(p) for p in canal]}

#anatomy below the skull blends between the head and neck bones like the game's own head skin
skin=numpy.array(reference['vertices']);skin_weights=numpy.array(reference['head_weights'])
lows=numpy.arange(-9.0,-1.95,0.1)[:,None];spans=numpy.arange(0.5,10.05,0.1)[None,:]
best=None
for slope in numpy.arange(0,0.81,0.02):
    level=skin[:,0]+slope*skin[:,1]
    error=numpy.zeros((lows.shape[0],spans.shape[1]))
    for s,w in zip(level,skin_weights):
        t=numpy.clip((s-lows)/spans,0,1);error+=(t*t*(3-2*t)-w)**2
    k=numpy.unravel_index(numpy.argmin(error),error.shape)
    if best is None or error[k]<best[0]:best=(float(error[k]),float(slope),float(lows[k[0],0]),float(lows[k[0],0]+spans[0,k[1]]))
_,bind_slope,bind_lo,bind_hi=best
def binding(point):
    return smooth(bind_lo,bind_hi,point.x+bind_slope*point.y)
fit_error=math.sqrt(sum((binding(Vector(p))-w)**2 for p,w in zip(reference['vertices'],reference['head_weights']))/len(skin))
report['neck'].update({'bind_slope':bind_slope,'bind_low':bind_lo,'bind_high':bind_hi,'bind_rms':fit_error})
outside={}
for obj in neck:
    count=0
    for v in obj.data.vertices:
        if v.co.x<-4:continue
        point,normal,_,_=head_bvh.find_nearest(v.co)
        count+=point is not None and (v.co-point).dot(normal)>0
    outside[obj.name]=count
report['neck']['vertices_outside_skin']=outside

cut_height=register_landmarks(Vector((7.5,0,0))).x
report['registration']['cut_height']=cut_height
bpy.ops.mesh.primitive_cube_add(size=1,location=(cut_height+5,2.0,5.0))
cutter = bpy.context.object
cutter.name = 'cutaway tool'
cutter.dimensions = (10,30,10)
bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
cutter.data.materials.append(materials[0]);cutter.data.materials.append(materials[2])
for p in cutter.data.polygons:
    p.material_index = 1
cut = []
for obj in skull:
    c = obj.copy();c.data=obj.data.copy();c.name=obj.name+'_cut'
    bpy.context.collection.objects.link(c)
    mod = c.modifiers.new('closed cutaway','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
    modifier(c,mod)
    for p in c.data.polygons:
        if p.material_index == 1:
            p.use_smooth = False
    cut.append(c)
bpy.data.objects.remove(cutter,do_unlink=True)

records = []
for obj,visible in [(o,1) for o in skull]+[(o,2) for o in cut]+[(o,3) for o in brain+teeth+neck]:
    obj.data.calc_loop_triangles()
    bm = bmesh.new();bm.from_mesh(obj.data)
    bad = sum(not e.is_manifold for e in bm.edges)
    bm.free()
    if bad:
        raise RuntimeError(f'{obj.name}: {bad} nonmanifold edges')
    vv = [v.co.copy() for v in obj.data.vertices]
    ff = [tuple(t.vertices) for t in obj.data.loop_triangles]
    bvh = BVHTree.FromPolygons(vv,ff,all_triangles=True) if ff else None
    occlusion=[]
    for v in obj.data.vertices:
        n=v.normal.normalized();t=n.cross(Vector((0,0,1)))
        if t.length < 0.1:t=n.cross(Vector((0,1,0)))
        t.normalize();b=n.cross(t)
        blocked=0
        for i in range(8):
            angle=i*2.399963
            d=(n*0.6+(t*math.cos(angle)+b*math.sin(angle))*0.8).normalized()
            point,_,_,_=bvh.ray_cast(v.co+n*0.025,d,1.3)
            blocked += point is not None
        occlusion.append(1-0.48*blocked/8)
    for slot,mat in enumerate(obj.data.materials):
        triangles=[t for t in obj.data.loop_triangles if t.material_index==slot]
        if not triangles:continue
        material_id=materials.index(mat)
        verts=[];indices=[];lookup={}
        for tri in triangles:
            poly=obj.data.polygons[tri.polygon_index]
            for index in tri.vertices:
                normal=obj.data.vertices[index].normal if poly.use_smooth else poly.normal
                assert 0.5<normal.length_squared<1.5,(obj.name,index,'invalid normal')
                key=(index,tuple(round(x,5) for x in normal))
                if key not in lookup:
                    lookup[key]=len(verts)
                    v=vv[index]
                    variation=0.99+0.01*math.sin(v.x*6+math.sin(v.y*7)*2+v.z*8)
                    verts.append((*v,*normal,occlusion[index]*variation,binding(v) if obj in neck else 1.0))
                indices.append(lookup[key])
        assert len(verts)<65536
        records.append((material_id,visible,verts,indices))
        report['meshes'].append({'name':obj.name,'material':material_id,'mode':visible,'vertices':len(verts),'triangles':len(indices)//3,'nonmanifold_edges':bad})
OUTPUT.parent.mkdir(parents=True,exist_ok=True)
with OUTPUT.open('wb') as f:
    f.write(struct.pack('<4sIII',b'XRA1',2,len(records),0))
    for mat,mode,vv,ii in records:
        f.write(struct.pack('<IIII',mat,mode,len(vv),len(ii)))
        for v in vv:f.write(struct.pack('<8f',*v))
        f.write(struct.pack('<'+'H'*len(ii),*ii))
report['bytes']=OUTPUT.stat().st_size
report['triangles_intact']=sum(len(i)//3 for m,s,v,i in records if s&1)
report['triangles_cutaway']=sum(len(i)//3 for m,s,v,i in records if s&2)
(ROOT/'assets/anatomy_report.json').write_text(json.dumps(report,indent=2))
for obj in skull:obj.hide_render=True;obj.hide_viewport=True
for obj in cut:obj.hide_render=False
bpy.ops.object.camera_add(location=(22,40,35))
camera=bpy.context.object;target=Vector((7,0,0));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=24;bpy.context.scene.camera=camera
for pos,power,size in (((30,30,25),42000,25),((0,-25,15),16000,25)):
    bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.data.energy=power;light.data.size=size;light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.world=bpy.data.worlds.new('world');scene.world.color=(0.04,)*3
scene.render.resolution_x=1000;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
(ROOT/'assets/previews').mkdir(exist_ok=True)
scene.render.filepath=str(ROOT/'assets/previews/fitted_cutaway.png');bpy.ops.render.render(write_still=True)
reference_mesh=ref.data
bpy.data.objects.remove(ref,do_unlink=True)
bpy.data.meshes.remove(reference_mesh)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/head_anatomy.blend'))
print('ANATOMY_BUILT',json.dumps({k:v for k,v in report.items() if k!='meshes'}),flush=True)
