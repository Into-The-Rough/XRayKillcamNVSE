import bpy
import json
import math
import sys
import numpy
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from anatomy_build import *

extract(('donors.zip','manifest.json'),('arm-donors.zip','arm-manifest.json'))
OUTPUT=ROOT/'mod/meshes/XRayKillcamNVSE/arms.xra'
PREVIEW='--preview' in sys.argv
bpy.ops.wm.read_factory_settings(use_empty=True)
report={'coordinate_space':'Bip01 Spine2 rest local, game units','meshes':[]}

#runtime influence table, index 0 is the frame bone itself
INFLUENCES=('Bip01 Spine2','Bip01 L UpperArm','Bip01 L Forearm','Bip01 L Hand','Bip01 R UpperArm','Bip01 R Forearm','Bip01 R Hand')
CARPALS=('scaphoid','lunate','triquetral','pisiform','trapezium','trapezoid','capitate','hamate')
METACARPALS=tuple(f'{n} metacarpal bone' for n in ('first','second','third','fourth','fifth'))
MATERIALS=make_materials()
ids=part_table()
donor=donor_loader(RESEARCH/'bodyparts3d/arms',ids)
body,rest,skin_to_bone=reference_body(hands=True)

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
    #ray parity, the body and hands are separate closed-ish shells that meet at the wrist
    votes=[crossings(point,d)%2 for d in (Vector((0,0,1)),Vector((0,1,0)),Vector((0,-1,0)))]
    return sum(votes)>=2
def outside(obj):
    return sum(not inside(v.co) for v in obj.data.vertices)

def bone(name):return numpy.array(donor(name)[0])
def low(points,depth):return points[points[:,2]<points[:,2].min()+depth].mean(0)
def high(points,depth):return points[points[:,2]>points[:,2].max()-depth].mean(0)
def joints(side):
    #shoulder from the humeral head, elbow and wrist from the joint surfaces, the knuckle from the third metacarpal head
    humerus=bone(f'{side} humerus');radius=bone(f'{side} radius');ulna=bone(f'{side} ulna')
    shoulder=head_centre(humerus,.3)
    elbow=(low(humerus,12)+high(radius,8))*.5
    lunate=bone(f'{side} lunate').mean(0)
    wrist=(low(radius,8)+lunate)*.5
    knuckle=low(bone(f'{side} third metacarpal bone'),8)
    thumb=bone(f'{side} first metacarpal bone').mean(0)
    #pronation turns the radius about the line from its head to the head of the ulna
    pivot=(high(radius,8),low(ulna,8))
    return shoulder,elbow,wrist,knuckle,thumb,pivot
def basis(axis,forward):
    u=Vector(axis).normalized();f=Vector(forward);f=(f-u*f.dot(u)).normalized();return u,f,u.cross(f)
def segment(donor_from,donor_to,game_from,game_to,donor_forward,game_forward,thickness):
    #carries the donor segment's axis and facing onto the game bone's, its length onto the bone's, its girth by the arm's scale
    du,df,ds=basis(Vector(donor_to)-Vector(donor_from),donor_forward)
    gu,gf,gs=basis(Vector(game_to)-Vector(game_from),game_forward)
    a=Vector(donor_from);b=Vector(game_from)
    length=(Vector(game_to)-Vector(game_from)).length/(Vector(donor_to)-Vector(donor_from)).length
    def place(p):
        d=Vector(p)-a;return b+gu*d.dot(du)*length+(gf*d.dot(df)+gs*d.dot(ds))*thickness
    return place
def across(point,start,end):
    #the part of point's offset from the line start-end that is perpendicular to it
    axis=(Vector(end)-Vector(start)).normalized();d=Vector(point)-Vector(start);return d-axis*d.dot(axis)

def piece(name,fid=None,budget=None):
    vv,ff=read_obj(RESEARCH/'bodyparts3d/arms'/f'{fid}.obj') if fid else donor(name)
    obj=mesh_object(name,vv,ff);reduce(obj,budget or max(80,len(ff)//4))
    if open_edges(obj):
        bpy.data.objects.remove(obj,do_unlink=True)
        obj=mesh_object(name,vv,ff);remesh(obj,0.6);keep_largest(obj,0.05);reduce(obj,budget or max(80,len(ff)//4))
        report.setdefault('remeshed_bones',[]).append(name)
    return obj

skeleton=[]
for side,letter in (('left','L'),('right','R')):
    shoulder,elbow,wrist,knuckle,thumb,pivot=joints(side)
    g_shoulder,g_elbow,g_wrist=[rest(f'Bip01 {letter} {n}')[3,:3] for n in ('UpperArm','Forearm','Hand')]
    g_knuckle,g_thumb=[rest(f'Bip01 {letter} {n}')[3,:3] for n in ('Finger2','Thumb1')]
    upper=numpy.linalg.norm(g_elbow-g_shoulder)/numpy.linalg.norm(elbow-shoulder)
    fore=numpy.linalg.norm(g_wrist-g_elbow)/numpy.linalg.norm(wrist-elbow)
    hand=numpy.linalg.norm(g_knuckle-g_wrist)/numpy.linalg.norm(knuckle-wrist)
    #one girth for the whole arm, so the joints still meet where the segments' length scales differ
    girth=(numpy.linalg.norm(g_elbow-g_shoulder)+numpy.linalg.norm(g_wrist-g_elbow))/(numpy.linalg.norm(elbow-shoulder)+numpy.linalg.norm(wrist-elbow))
    #the hand faces by its thumb, the arm segments by the body's front
    maps={'upper':segment(shoulder,elbow,g_shoulder,g_elbow,(0,-1,0),(0,1,0),girth),'fore':segment(elbow,wrist,g_elbow,g_wrist,(0,-1,0),(0,1,0),girth),
        'hand':segment(wrist,knuckle,g_wrist,g_knuckle,across(thumb,wrist,knuckle),across(g_thumb,g_wrist,g_knuckle),girth)}
    base=INFLUENCES.index(f'Bip01 {letter} UpperArm')
    for label,part,influence,budget in (('humerus','upper',0,2600),('ulna','fore',1,1800),('radius','fore',1,1600)):
        obj=piece(f'{side} {label}',budget=budget)
        for v in obj.data.vertices:v.co=maps[part](v.co)
        obj.data.update();obj.data.materials.append(MATERIALS[BONE])
        obj['first']=obj['second']=base+influence;obj['binds']=[1.0]*len(obj.data.vertices);skeleton.append(obj)
    #the donor forearm is supinated and the game's rest hand is turned palm down, so the radius turns about its pronation axis
    #until its far end lies on the thumb side, where the game's hand puts the thumb
    radius=skeleton[-1];head=Vector(maps['fore'](pivot[0]));tail=Vector(maps['fore'](pivot[1]))
    far=Vector(maps['fore'](low(bone(f'{side} radius'),8)))
    have=across(far,head,tail);want=across(Vector(g_thumb),head,tail)
    axis=(tail-head).normalized();turn=math.atan2(axis.dot(have.cross(want)),have.dot(want))
    spin=Matrix.Rotation(turn,3,axis)
    for v in radius.data.vertices:v.co=head+spin@(v.co-head)
    radius.data.update()
    #each carpal and metacarpal keeps its own closed surface, reduced on its own and joined into one simplified hand
    pieces=[piece(f'{side} hand {n}',fid) for n in CARPALS+METACARPALS for fid in ids(f'{side} {n}')]
    bpy.ops.object.select_all(action='DESELECT')
    for p in pieces:p.select_set(True)
    bpy.context.view_layer.objects.active=pieces[0];bpy.ops.object.join()
    palm=pieces[0];palm.name=f'{side} hand'
    reduce(palm,3600);tidy(palm)
    if open_edges(palm):
        remesh(palm,0.6);keep_largest(palm,0.01);reduce(palm,3600);tidy(palm)
        report.setdefault('remeshed_bones',[]).append(f'{side} hand')
    for v in palm.data.vertices:v.co=maps['hand'](v.co)
    palm.data.update();palm.data.materials.clear();palm.data.materials.append(MATERIALS[BONE])
    palm['first']=palm['second']=base+2;palm['binds']=[1.0]*len(palm.data.vertices);skeleton.append(palm)
    report.setdefault('registration',{})[side]={'upper_length_scale':upper,'fore_length_scale':fore,'hand_length_scale':hand,'girth_scale':girth,'radius_pronation_degrees':math.degrees(turn),
        'donor_shoulder':list(shoulder),'donor_elbow':list(elbow),'donor_wrist':list(wrist),'donor_knuckle':list(knuckle),
        'game_shoulder':list(g_shoulder),'game_elbow':list(g_elbow),'game_wrist':list(g_wrist),'game_knuckle':list(g_knuckle)}

#each segment's bones are pulled radially under the skin around its own axis, repeated until they stay inside
CLEARANCE,ANGLES,STEP=0.25,36,0.4
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
            t=a*math.tau/ANGLES;hit=game_bvh.ray_cast(centre,side*math.cos(t)+front*math.sin(t),20)
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
    g_shoulder,g_elbow,g_wrist,g_knuckle=[rest(f'Bip01 {letter} {n}')[3,:3] for n in ('UpperArm','Forearm','Hand','Finger2')]
    named=lambda *labels:[o for o in skeleton if o.name in [f'{side} {l}' for l in labels]]
    report.setdefault('cage',{})[side]={'upper':cage(named('humerus'),g_shoulder,g_elbow),'fore':cage(named('ulna','radius'),g_elbow,g_wrist),
        'hand':cage(named('hand'),g_wrist,g_knuckle)}

report['outside_skin']={o.name:outside(o) for o in skeleton}

if PREVIEW:
    ref=mesh_object('game body',[tuple(v) for v in game_vertices],game_faces)
    glass=material('skin',(0.8,0.65,0.55),0.5);ref.data.materials.append(glass)
    bsdf=glass.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Alpha'].default_value=0.18
    target=Vector((-30,0,92))
    for name,location in (('front',(-30,110,95)),('top',(-30,1,200))):
        bpy.ops.object.camera_add(location=location)
        camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        camera.data.type='ORTHO';camera.data.ortho_scale=45;bpy.context.scene.camera=camera
        if not any(o.type=='LIGHT' for o in bpy.data.objects):
            for pos,power in (((-30,80,160),60000),((-30,-60,60),25000)):
                bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.data.energy=power;light.data.size=60
                light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
        scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.world=scene.world or bpy.data.worlds.new('world')
        scene.world.color=(0.04,)*3;scene.render.resolution_x=900;scene.render.resolution_y=900
        scene.render.filepath=str(ROOT/f'assets/previews/arms_{name}.png');bpy.ops.render.render(write_still=True)

#positions leave the build in rest Spine2 space
to_frame(skeleton,skin_to_bone('Bip01 Spine2'))
export(skeleton,MATERIALS,OUTPUT,report)
(ROOT/'assets/arms_report.json').write_text(json.dumps(report,indent=2))
for obj in [o for o in bpy.data.objects if o.name=='game body']:
    mesh=obj.data;bpy.data.objects.remove(obj,do_unlink=True);bpy.data.meshes.remove(mesh)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/arms_anatomy.blend'))
print('ARMS_BUILT',json.dumps({k:v for k,v in report.items() if k in ('outside_skin','bytes','triangles','remeshed_bones','cage','registration')}),flush=True)
