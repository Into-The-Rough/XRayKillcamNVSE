import bpy
import bmesh
import json
import math
import sys
import numpy
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from anatomy_build import *

extract(('donors.zip','manifest.json'),('limb-donors.zip','limb-manifest.json'))
OUTPUT=ROOT/'mod/meshes/XRayKillcamNVSE/legs.xra'
PREVIEW='--preview' in sys.argv
bpy.ops.wm.read_factory_settings(use_empty=True)
report={'coordinate_space':'Bip01 Pelvis rest local, game units','meshes':[]}

#runtime influence table, index 0 is the frame bone itself
INFLUENCES=('Bip01 Pelvis','Bip01 L Thigh','Bip01 L Calf','Bip01 L Foot','Bip01 R Thigh','Bip01 R Calf','Bip01 R Foot')
MATERIALS=make_materials()
ids=part_table()
donor=donor_loader(RESEARCH/'bodyparts3d/limbs',ids)
body,rest,skin_to_bone=reference_body()

game_vertices=[];game_faces=[]
for shape in body:
    start=len(game_vertices)
    game_vertices.extend(Vector(v) for v in shape['vertices'])
    game_faces.extend(tuple(start+i for i in t) for t in shape['triangles'])
game_bvh=BVHTree.FromPolygons(game_vertices,game_faces,all_triangles=True)
def crossings(point,direction):
    count=0;origin=Vector(point)
    while count<64:
        hit=game_bvh.ray_cast(origin,direction,500)
        if hit[0] is None:break
        count+=1;origin=hit[0]+direction*1e-4
    return count
def inside(point):
    #ray parity, the nearest-normal test misjudges concave skin such as the crotch
    votes=[crossings(point,d)%2 for d in (Vector((1,0,0)),Vector((0,1,0)),Vector((0,-1,0)))]
    return sum(votes)>=2
def outside(obj):
    return sum(not inside(v.co) for v in obj.data.vertices)

def centroid(points):return numpy.array(points).mean(0)
def joints(side):
    #hip from the femoral head, knee and ankle from the joint surfaces, the ball from the metatarsal heads
    femur=numpy.array(donor(f'{side} femur')[0]);tibia=numpy.array(donor(f'{side} tibia')[0]);talus=numpy.array(donor(f'{side} talus')[0])
    hip=head_centre(femur,.6)
    knee=(femur[femur[:,2]<femur[:,2].min()+25].mean(0)+tibia[tibia[:,2]>tibia[:,2].max()-15].mean(0))*.5
    ankle=(tibia[tibia[:,2]<tibia[:,2].min()+12].mean(0)+talus[talus[:,2]>talus[:,2].max()-10].mean(0))*.5
    heads=[]
    for n in ('first','second','third','fourth','fifth'):
        bone=numpy.array(donor(f'{side} {n} metatarsal bone')[0]);heads.append(bone[numpy.argsort(bone[:,1])[:40]].mean(0))
    return hip,knee,ankle,centroid(heads)
def basis(axis,forward):
    u=Vector(axis).normalized();f=Vector(forward);f=(f-u*f.dot(u)).normalized();return u,f,u.cross(f)
def segment(donor_from,donor_to,game_from,game_to,donor_forward,game_forward,scale):
    #a similarity carrying the donor segment's axis and facing onto the game bone's
    du,df,ds=basis(Vector(donor_to)-Vector(donor_from),donor_forward)
    gu,gf,gs=basis(Vector(game_to)-Vector(game_from),game_forward)
    a=Vector(donor_from);b=Vector(game_from)
    def place(p):
        d=Vector(p)-a;return b+(gu*d.dot(du)+gf*d.dot(df)+gs*d.dot(ds))*scale
    return place

skeleton=[]
for side,letter in (('left','L'),('right','R')):
    hip,knee,ankle,ball=joints(side)
    g_hip,g_knee,g_ankle,g_toe=[rest(f'Bip01 {letter} {n}')[3,:3] for n in ('Thigh','Calf','Foot','Toe0')]
    thigh=numpy.linalg.norm(g_knee-g_hip)/numpy.linalg.norm(knee-hip);shank=numpy.linalg.norm(g_ankle-g_knee)/numpy.linalg.norm(ankle-knee)
    #the game's toe bone sits ahead of the ball of the foot, so the foot keeps the shank's scale and only takes the game foot's direction
    maps={'thigh':segment(hip,knee,g_hip,g_knee,(0,-1,0),(0,1,0),thigh),'shank':segment(knee,ankle,g_knee,g_ankle,(0,-1,0),(0,1,0),shank),
        'foot':segment(ankle,ball,g_ankle,g_toe,(0,0,1),(0,0,1),shank)}
    report.setdefault('registration',{})[side]={'thigh_scale':thigh,'shank_scale':shank,'donor_hip':list(hip),'donor_knee':list(knee),'donor_ankle':list(ankle),
        'donor_ball':list(ball),'game_hip':list(g_hip),'game_knee':list(g_knee),'game_ankle':list(g_ankle),'game_toe':list(g_toe)}
    base=INFLUENCES.index(f'Bip01 {letter} Thigh')
    for label,name,part,influence,budget in (('femur','femur','thigh',0,3000),('patella','patella','thigh',1,600),('tibia','tibia','shank',1,2600),
            ('fibula','fibula','shank',1,1400)):
        vv,ff=donor(f'{side} {name}')
        obj=mesh_object(f'{side} {label}',vv,ff);reduce(obj,budget)
        if open_edges(obj):
            bpy.data.objects.remove(obj,do_unlink=True)
            obj=mesh_object(f'{side} {label}',vv,ff);remesh(obj,0.7);keep_largest(obj,0.05);reduce(obj,budget)
            report.setdefault('remeshed_bones',[]).append(f'{side} {label}')
        for v in obj.data.vertices:v.co=maps[part](v.co)
        obj.data.update();obj.data.materials.append(MATERIALS[BONE])
        obj['first']=obj['second']=base+influence;obj['binds']=[1.0]*len(obj.data.vertices);skeleton.append(obj)
    #each foot bone keeps its own closed surface, reduced on its own and joined into one simplified foot
    pieces=[]
    for fid in ids(f'{side} foot'):
        vv,ff=read_obj(RESEARCH/'bodyparts3d/limbs'/f'{fid}.obj')
        piece=mesh_object(f'{side} foot {fid}',vv,ff);reduce(piece,max(80,len(ff)//4))
        if open_edges(piece):
            bpy.data.objects.remove(piece,do_unlink=True)
            piece=mesh_object(f'{side} foot {fid}',vv,ff);remesh(piece,0.6);keep_largest(piece,0.05);reduce(piece,max(80,len(ff)//4))
        pieces.append(piece)
    bpy.ops.object.select_all(action='DESELECT')
    for piece in pieces:piece.select_set(True)
    bpy.context.view_layer.objects.active=pieces[0];bpy.ops.object.join()
    foot=pieces[0];foot.name=f'{side} foot'
    reduce(foot,5000);tidy(foot)
    if open_edges(foot):
        remesh(foot,0.6);keep_largest(foot,0.01);reduce(foot,5000);tidy(foot)
        report.setdefault('remeshed_bones',[]).append(f'{side} foot')
    for v in foot.data.vertices:v.co=maps['foot'](v.co)
    foot.data.update();foot.data.materials.clear();foot.data.materials.append(MATERIALS[BONE])
    foot['first']=foot['second']=base+2;foot['binds']=[1.0]*len(foot.data.vertices);skeleton.append(foot)

#each segment's bone is pulled radially under the skin around its own axis, repeated until it stays inside
CLEARANCE,ANGLES,STEP=0.3,36,0.5
def cage(objects,start,end):
    start=Vector(start);axis=Vector(end)-start;length=axis.length;axis.normalize()
    side=axis.cross(Vector((0,0,1)) if abs(axis.z)<.9 else Vector((1,0,0))).normalized();front=axis.cross(side)
    levels=int(length/STEP)+3
    def index(p):
        d=p-start;h=d.dot(axis);r=d-axis*h
        return min(max(h/STEP+1,0),levels-1),(math.atan2(r.dot(front),r.dot(side))%math.tau)*ANGLES/math.tau,r.length,h
    radii=numpy.full((levels,ANGLES),1e9)
    for k in range(levels):
        centre=start+axis*((k-1)*STEP)
        for a in range(ANGLES):
            t=a*math.tau/ANGLES;hit=game_bvh.ray_cast(centre,side*math.cos(t)+front*math.sin(t),40)
            if hit[0] is not None:radii[k,a]=hit[3]
    passes=[]
    for attempt in range(5):
        factors=numpy.ones((levels,ANGLES))
        for obj in objects:
            for v in obj.data.vertices:
                h,a,r,_=index(v.co)
                if r>0.01:factors[int(round(h)),int(round(a))%ANGLES]=min(factors[int(round(h)),int(round(a))%ANGLES],(radii[int(round(h)),int(round(a))%ANGLES]-CLEARANCE)/r)
        factors=numpy.clip(factors,0.6,1)
        for _ in range(2):
            factors=(factors+numpy.roll(factors,1,1)+numpy.roll(factors,-1,1)+numpy.vstack([factors[:1],factors[:-1]])+numpy.vstack([factors[1:],factors[-1:]]))/5
        for obj in objects:
            for v in obj.data.vertices:
                h,a,r,along=index(v.co);h0=int(h);h1=min(levels-1,h0+1);fh=h-h0;a0=int(a)%ANGLES;a1=(a0+1)%ANGLES;fa=a-int(a)
                f=(factors[h0,a0]*(1-fa)+factors[h0,a1]*fa)*(1-fh)+(factors[h1,a0]*(1-fa)+factors[h1,a1]*fa)*fh
                centre=start+axis*along;v.co=centre+(v.co-centre)*f
            obj.data.update()
        passes.append(sum(outside(o) for o in objects))
        if not passes[-1]:break
    return passes
for side,letter in (('left','L'),('right','R')):
    g_hip,g_knee,g_ankle,g_toe=[rest(f'Bip01 {letter} {n}')[3,:3] for n in ('Thigh','Calf','Foot','Toe0')]
    named=lambda *labels:[o for o in skeleton if o.name in [f'{side} {l}' for l in labels]]
    report.setdefault('cage',{})[side]={'thigh':cage(named('femur','patella'),g_hip,g_knee),'shank':cage(named('tibia','fibula'),g_knee,g_ankle),
        'foot':cage(named('foot'),g_ankle,g_toe)}

report['outside_skin']={o.name:outside(o) for o in skeleton}

if PREVIEW:
    ref=mesh_object('game body',[tuple(v) for v in game_vertices],game_faces)
    glass=material('skin',(0.8,0.65,0.55),0.5);ref.data.materials.append(glass)
    bsdf=glass.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Alpha'].default_value=0.18
    target=Vector((0,1,36))
    for name,location in (('front',(25,110,40)),('side',(110,5,40))):
        bpy.ops.object.camera_add(location=location)
        camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        camera.data.type='ORTHO';camera.data.ortho_scale=80;bpy.context.scene.camera=camera
        if not any(o.type=='LIGHT' for o in bpy.data.objects):
            for pos,power in (((60,80,100),60000),((-60,-60,60),25000)):
                bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.data.energy=power;light.data.size=60
                light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
        scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.world=scene.world or bpy.data.worlds.new('world')
        scene.world.color=(0.04,)*3;scene.render.resolution_x=900;scene.render.resolution_y=900
        scene.render.filepath=str(ROOT/f'assets/previews/legs_{name}.png');bpy.ops.render.render(write_still=True)

#positions leave the build in rest Pelvis space
to_frame(skeleton,skin_to_bone('Bip01 Pelvis'))
export(skeleton,MATERIALS,OUTPUT,report)
(ROOT/'assets/legs_report.json').write_text(json.dumps(report,indent=2))
for obj in [o for o in bpy.data.objects if o.name=='game body']:
    mesh=obj.data;bpy.data.objects.remove(obj,do_unlink=True);bpy.data.meshes.remove(mesh)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/legs_anatomy.blend'))
print('LEGS_BUILT',json.dumps({k:v for k,v in report.items() if k in ('outside_skin','bytes','triangles','remeshed_bones','cage')}),flush=True)
