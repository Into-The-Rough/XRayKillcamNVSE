import bpy
import bmesh
import json
import math
import sys
import numpy
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from anatomy_build import *

extract(('donors.zip','manifest.json'),('torso-donors.zip','torso-manifest.json'),('genital-donors.zip','genital-manifest.json'))
OUTPUT=ROOT/'mod/meshes/XRayKillcamNVSE/torso.xra'
PREVIEW='--preview' in sys.argv
bpy.ops.wm.read_factory_settings(use_empty=True)
report={'coordinate_space':'Bip01 Spine1 rest local; X up the spine, game units','meshes':[]}

#runtime influence table, index 0 is the frame bone itself
INFLUENCES=('Bip01 Spine1','Bip01 Spine2','Bip01 Spine','Bip01 Pelvis','Bip01 Neck1','Bip01 L Clavicle','Bip01 R Clavicle')
#game bones whose skin weight counts toward a torso influence
#the shoulder girdle follows the clavicle, arm weight near it counts toward that bone
FOLD={'Bip01 L Thigh':'Bip01 Pelvis','Bip01 R Thigh':'Bip01 Pelvis','Bip01 L UpperArm':'Bip01 L Clavicle','Bip01 R UpperArm':'Bip01 R Clavicle',
    'Bip01 LUpArmTwistBone':'Bip01 L Clavicle','Bip01 RUpArmTwistBone':'Bip01 R Clavicle',
    'Bip01 L Forearm':'Bip01 L Clavicle','Bip01 R Forearm':'Bip01 R Clavicle','Bip01 L ForeTwist':'Bip01 L Clavicle','Bip01 R ForeTwist':'Bip01 R Clavicle',
    'Bip01 L Hand':'Bip01 L Clavicle','Bip01 R Hand':'Bip01 R Clavicle','Bip01 L Calf':'Bip01 Pelvis','Bip01 R Calf':'Bip01 Pelvis',
    'Bip01 L Foot':'Bip01 Pelvis','Bip01 R Foot':'Bip01 Pelvis','Bip01 L Toe0':'Bip01 Pelvis','Bip01 R Toe0':'Bip01 Pelvis'}


MATERIALS=make_materials()
ids=part_table()
ORDINALS=('first','second','third','fourth','fifth','sixth','seventh','eighth','ninth','tenth','eleventh','twelfth')
TORSO=RESEARCH/'bodyparts3d/torso'
donor=donor_loader(TORSO,ids)

def joint(name,lean):return head_centre(donor(name)[0],lean)
donor_hips=[joint(f'{s} femur',.6) for s in ('left','right')]
donor_shoulders=[joint(f'{s} humerus',.3) for s in ('left','right')]

body,rest,skin_to_bone=reference_body()
game_hips=[rest(n)[3,:3] for n in ('Bip01 L Thigh','Bip01 R Thigh')]
game_shoulders=[rest(n)[3,:3] for n in ('Bip01 L UpperArm','Bip01 R UpperArm')]
donor_hip_z=(donor_hips[0][2]+donor_hips[1][2])*.5;donor_shoulder_z=(donor_shoulders[0][2]+donor_shoulders[1][2])*.5
game_hip_z=(game_hips[0][2]+game_hips[1][2])*.5;game_shoulder_z=(game_shoulders[0][2]+game_shoulders[1][2])*.5
SCALE=(game_shoulder_z-game_hip_z)/(donor_shoulder_z-donor_hip_z)
game_mid_x=(game_hips[0][0]+game_hips[1][0])*.25+(game_shoulders[0][0]+game_shoulders[1][0])*.25
shoulder_ratio=abs(game_shoulders[0][0]-game_shoulders[1][0])/abs(donor_shoulders[0][0]-donor_shoulders[1][0])/SCALE
report['registration']={'scale':SCALE,'donor_hip_height':donor_hip_z,'donor_shoulder_height':donor_shoulder_z,
    'game_hip_height':game_hip_z,'game_shoulder_height':game_shoulder_z,'shoulder_width_ratio':shoulder_ratio}

game_vertices=[];game_faces=[];game_weights=[]
for shape in body:
    start=len(game_vertices);count=len(shape['vertices'])
    game_vertices.extend(Vector(v) for v in shape['vertices'])
    game_faces.extend(tuple(start+i for i in t) for t in shape['triangles'])
    weights=numpy.zeros((count,len(INFLUENCES)))
    for name,bone in shape['bones'].items():
        target=FOLD.get(name,name)
        if target in INFLUENCES:weights[:,INFLUENCES.index(target)]+=bone['weights']
    game_weights.extend(weights)
game_weights=numpy.array(game_weights)
game_bvh=BVHTree.FromPolygons(game_vertices,game_faces,all_triangles=True)
skin_vv,skin_ff=donor('skin')
donor_bvh=BVHTree.FromPolygons([Vector(v) for v in skin_vv],[f[:3] for f in skin_ff],all_triangles=True)

def cast(bvh,origin,direction,distance=1000):
    hit=bvh.ray_cast(Vector(origin),Vector(direction),distance)
    return hit[3] if hit[0] is not None else None

#per level of the game torso: donor and game skin along the midline, front and back, and the half width
LEVELS=numpy.arange(56.0,113.0,1.0)
profile=[]
for zg in LEVELS:
    zd=donor_hip_z+(zg-game_hip_z)/SCALE
    df=cast(donor_bvh,(0,-100,zd),(0,-1,0));db=cast(donor_bvh,(0,-100,zd),(0,1,0));dw=cast(donor_bvh,(0,-100,zd),(1,0,0))
    gf=cast(game_bvh,(game_mid_x,.5,zg),(0,1,0));gb=cast(game_bvh,(game_mid_x,.5,zg),(0,-1,0));gw=cast(game_bvh,(game_mid_x,.5,zg),(1,0,0))
    profile.append([zg,None if df is None else 100+df,None if db is None else 100-db,dw,None if gf is None else .5+gf,None if gb is None else .5-gb,gw])
def column(k,lo,hi):
    #a column valid between two game heights, held constant outside them and smoothed along the body
    values=numpy.array([p[k] if p[k] is not None and lo<=p[0]<=hi else numpy.nan for p in profile],dtype=float)
    valid=~numpy.isnan(values);values=numpy.interp(LEVELS,LEVELS[valid],values[valid])
    kernel=numpy.exp(-.5*(numpy.arange(-6,7)/2.0)**2);kernel/=kernel.sum()
    padded=numpy.pad(values,6,mode='edge');return numpy.convolve(padded,kernel,mode='valid')
#below the hips the legs split the midline, above the collar the neck narrows the profile
DONOR_FRONT=column(1,63,104);DONOR_BACK=column(2,63,106);GAME_FRONT=column(4,63,104);GAME_BACK=column(5,63,106)
#the donor's hanging arms widen its profile above the armpit, so the shoulder joints set the width there
width=numpy.array([(p[6]/(p[3]*SCALE)) if p[3] and p[6] and 63<=p[0]<=92 else numpy.nan for p in profile],dtype=float)
valid=~numpy.isnan(width);width=numpy.interp(LEVELS,LEVELS[valid],width[valid])
blend=numpy.array([smoothstep(90,game_shoulder_z,z) for z in LEVELS]);WIDTH=width*(1-blend)+shoulder_ratio*blend
report['registration']['levels']=[{'height':float(z),'donor_front':float(a),'donor_back':float(b),'game_front':float(c),'game_back':float(d),'width':float(w)}
    for z,a,b,c,d,w in zip(LEVELS,DONOR_FRONT,DONOR_BACK,GAME_FRONT,GAME_BACK,WIDTH)]

def register(point):
    x,y,z=point
    zg=game_hip_z+(z-donor_hip_z)*SCALE
    df,db=numpy.interp(zg,LEVELS,DONOR_FRONT),numpy.interp(zg,LEVELS,DONOR_BACK)
    gf,gb=numpy.interp(zg,LEVELS,GAME_FRONT),numpy.interp(zg,LEVELS,GAME_BACK)
    t=(-y-db)/(df-db)
    return Vector((game_mid_x-x*SCALE*numpy.interp(zg,LEVELS,WIDTH),gb+t*(gf-gb),zg))

#skeleton: each rigid bone keeps its own donor mesh
skeleton=[]
def bone_part(label,name,slot,budget):
    vv,ff=donor(name)
    obj=mesh_object(label,vv,ff);reduce(obj,budget)
    if open_edges(obj):
        #a few donor parts are not closed or lose closure when reduced, a fine voxel shell restores it
        bpy.data.objects.remove(obj,do_unlink=True)
        obj=mesh_object(label,vv,ff);remesh(obj,0.7);keep_largest(obj,0.05);reduce(obj,budget)
        report.setdefault('remeshed_bones',[]).append(label)
    obj.data.materials.append(MATERIALS[slot]);obj['source']=name;obj['kind']='bone' if slot==BONE else 'cartilage'
    skeleton.append(obj);return obj
for i,o in enumerate(ORDINALS):bone_part(f'T{i+1}',f'{o} thoracic vertebra',BONE,1300)
for i,o in enumerate(ORDINALS[:5]):bone_part(f'L{i+1}',f'{o} lumbar vertebra',BONE,1400)
bone_part('sacrum','sacrum',BONE,2400)
for side in ('left','right'):
    for i,o in enumerate(ORDINALS):bone_part(f'{side} rib {i+1}',f'{side} {o} rib',BONE,900)
    for i,o in enumerate(ORDINALS[:7]):bone_part(f'{side} costal cartilage {i+1}',f'{side} {o} costal cartilage',CARTILAGE,500)
    bone_part(f'{side} clavicle',f'{side} clavicle',BONE,900)
    bone_part(f'{side} scapula',f'{side} scapula',BONE,2600)
    bone_part(f'{side} hip bone',f'{side} hip bone',BONE,3200)
bone_part('manubrium','manubrium',BONE,800)
bone_part('sternum','body of sternum',BONE,900)
bone_part('xiphoid','xiphoid process',BONE,300)

#organs are many donor elements each, a voxel remesh closes them into one solid
organs=[]
def organ(label,names,slot,voxel,budget,source=None):
    vertices=[];faces=[]
    for name in names:
        vv,ff=(source or donor)(name);start=len(vertices);vertices.extend(vv);faces.extend(tuple(start+i for i in f) for f in ff)
    obj=mesh_object(label,vertices,faces);remesh(obj,voxel)
    islands,removed=keep_largest(obj,0.05)
    reduce(obj,budget)
    obj.data.materials.append(MATERIALS[slot]);obj['source']=','.join(names);obj['kind']='organ'
    report.setdefault('organs',{})[label]={'islands':islands,'removed_vertices':removed}
    organs.append(obj);return obj
heart=organ('heart',['heart'],HEART,2.0,8000)
liver=organ('liver',['liver'],LIVER,2.5,7000)
stomach=organ('stomach',['stomach'],GUT,2.0,3000)
#bodyparts3d models the lungs only as their airway and vessel trees, the tissue around them is the tree's hull
#grown to the pleura, with the heart and the organs under the diaphragm carved out of it
def lung(label,name,avoid,budget):
    vv,ff=donor(name)
    bm=bmesh.new()
    for v in vv:bm.verts.new(v)
    bmesh.ops.convex_hull(bm,input=list(bm.verts))
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
    mesh=bpy.data.meshes.new(label);bm.to_mesh(mesh);bm.free()
    obj=bpy.data.objects.new(label,mesh);bpy.context.collection.objects.link(obj)
    stages=[('hull',len(obj.data.vertices))]
    remesh(obj,3.0);stages.append(('remeshed',len(obj.data.vertices)))
    smooth=obj.modifiers.new('rounded','SMOOTH');smooth.factor=1.0;smooth.iterations=12;apply_modifier(obj,smooth)
    grow=obj.modifiers.new('to the pleura','DISPLACE');grow.strength=8.0;grow.mid_level=0.0;apply_modifier(obj,grow)
    remesh(obj,3.0);stages.append(('grown',len(obj.data.vertices)))
    for other in avoid:
        #the organ's hull is a closed solid even where the organ keeps inner cavities the boolean cannot classify
        bm=bmesh.new()
        for v in other.data.vertices:bm.verts.new(v.co)
        bmesh.ops.convex_hull(bm,input=list(bm.verts))
        bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS')
        hull=bpy.data.meshes.new(other.name+' hull');bm.to_mesh(hull);bm.free()
        cutter=bpy.data.objects.new(other.name+' hull',hull);bpy.context.collection.objects.link(cutter)
        remesh(cutter,3.0)
        pad=cutter.modifiers.new('clearance','DISPLACE');pad.strength=3.0;pad.mid_level=0.0;apply_modifier(cutter,pad)
        cut=obj.modifiers.new('carved','BOOLEAN');cut.operation='DIFFERENCE';cut.solver='EXACT';cut.object=cutter;apply_modifier(obj,cut)
        mesh=cutter.data;bpy.data.objects.remove(cutter,do_unlink=True);bpy.data.meshes.remove(mesh)
        stages.append(('without '+other.name,len(obj.data.vertices)))
    report.setdefault('lung_stages',{})[label]=stages
    remesh(obj,2.5);islands,removed=keep_largest(obj,0.05);reduce(obj,budget)
    obj.data.materials.clear();obj.data.materials.append(MATERIALS[LUNG]);obj['source']=name;obj['kind']='organ'
    for p in obj.data.polygons:p.material_index=0
    report.setdefault('organs',{})[label]={'islands':islands,'removed_vertices':removed,'from':'hull of the airway and vessel tree'}
    organs.append(obj);return obj
lung('left lung','left lung',[heart,stomach],6000)
lung('right lung','right lung',[heart,liver],6500)
organ('intestines',['duodenum','jejunum','ileum','large intestine'],GUT,4.0,12000)
organ('left kidney',['left kidney'],KIDNEY,2.0,2000)
organ('right kidney',['right kidney'],KIDNEY,2.0,2000)
#the external genitals come from the is-a archive and stay a group of their own, women's anatomy leaves them out
parts=json.loads((ROOT/'assets/source/genital-manifest.json').read_text())['parts']
genital_donor=donor_loader(RESEARCH/'bodyparts3d/genitals',lambda name:parts[name])
genitals=[organ(f'{side} testis',[f'{side} testis',f'{side} epididymis'],TESTIS,1.2,1600,genital_donor) for side in ('left','right')]
genitals.append(organ('penis',['corpus cavernosum of penis','corpus spongiosum of penis','glans penis'],PENIS,1.2,2400,genital_donor))
for obj in genitals:organs.remove(obj)

anatomy=skeleton+organs
for obj in anatomy:
    for v in obj.data.vertices:v.co=register(v.co)
    obj.data.update()

#the radial cage pulls bone that breaks the game skin back under it, organs follow the same cage
CLEARANCE=0.6
CAGE_LOW,CAGE_HIGH=64.0,104.0
centre_y=(GAME_FRONT+GAME_BACK)*.5
ANGLES=48
def cage_index(point):
    zg=point.z;level=int(round(min(max(zg,LEVELS[0]),LEVELS[-1])-LEVELS[0]))
    cy=numpy.interp(zg,LEVELS,centre_y);dx=point.x-game_mid_x;dy=point.y-cy
    return level,(math.atan2(dy,dx)%math.tau)*ANGLES/math.tau,math.hypot(dx,dy),cy
radii=numpy.full((len(LEVELS),ANGLES),1e9)
for k,zg in enumerate(LEVELS):
    cy=centre_y[k]
    for a in range(ANGLES):
        theta=a*math.tau/ANGLES;d=cast(game_bvh,(game_mid_x,cy,zg),(math.cos(theta),math.sin(theta),0),60)
        if d is not None:radii[k,a]=d
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
#smoothing lets a few vertices back out, so the cage repeats until the skeleton stays inside
for attempt in range(4):
    factors=numpy.ones((len(LEVELS),ANGLES))
    for obj in skeleton:
        for v in obj.data.vertices:
            level,at,r,_=cage_index(v.co)
            a=int(round(at))%ANGLES
            if r>0.01:factors[level,a]=min(factors[level,a],(radii[level,a]-CLEARANCE)/r)
    factors=numpy.clip(factors,0.7,1)
    #below the crotch and above the collar a level is not one closed ring of skin
    factors[(LEVELS<CAGE_LOW)|(LEVELS>CAGE_HIGH)]=1
    for _ in range(3):
        factors=(factors+numpy.roll(factors,1,1)+numpy.roll(factors,-1,1)+numpy.vstack([factors[:1],factors[:-1]])+numpy.vstack([factors[1:],factors[-1:]]))/5
    def caged(point):
        level,at,r,cy=cage_index(point)
        zt=min(max(point.z,LEVELS[0]),LEVELS[-1])-LEVELS[0];l0=int(zt);l1=min(len(LEVELS)-1,l0+1);fz=zt-l0
        a0=int(at)%ANGLES;a1=(a0+1)%ANGLES;fa=at-int(at)
        f=(factors[l0,a0]*(1-fa)+factors[l0,a1]*fa)*(1-fz)+(factors[l1,a0]*(1-fa)+factors[l1,a1]*fa)*fz
        return Vector((game_mid_x+(point.x-game_mid_x)*f,cy+(point.y-cy)*f,point.z))
    for obj in anatomy:
        for v in obj.data.vertices:v.co=caged(v.co)
        obj.data.update()
    report['registration'].setdefault('cage_passes',[]).append(sum(outside(o) for o in skeleton))
    if report['registration']['cage_passes'][-1]==0:break

#bodyparts3d has no spinal cord, a tube follows the canal measured through the registered vertebrae down to the sacrum
column=[o for o in skeleton if o.name[0] in 'TL' and o.name[1:].isdigit()]+[next(o for o in skeleton if o.name=='sacrum')]
spine_vertices=[];spine_faces=[]
for obj in column:
    obj.data.calc_loop_triangles();start=len(spine_vertices)
    spine_vertices.extend(v.co.copy() for v in obj.data.vertices)
    spine_faces.extend(tuple(start+i for i in t.vertices) for t in obj.data.loop_triangles)
spine_bvh=BVHTree.FromPolygons(spine_vertices,spine_faces,all_triangles=True)
canal=[]
for obj in column:
    lo=min(v.co.z for v in obj.data.vertices);hi=max(v.co.z for v in obj.data.vertices)
    for fraction in (0.3,0.5,0.7):
        height=lo+(hi-lo)*fraction;hits=[];origin=Vector((game_mid_x,30,height))
        while len(hits)<16:
            hit=spine_bvh.ray_cast(origin,Vector((0,-1,0)),80)
            if hit[0] is None:break
            hits.append(hit[0].y);origin=Vector((game_mid_x,hit[0].y-1e-4,height))
        gaps=[(hits[k]-hits[k+1],(hits[k]+hits[k+1])*0.5) for k in range(1,len(hits)-1,2)]
        gaps=[g for g in gaps if 0.5<g[0]<2.8]
        if gaps:canal.append(Vector((game_mid_x,max(gaps)[1],height)))
canal.sort(key=lambda p:-p.z)
canal=[p for i,p in enumerate(canal) if abs(p.y-sorted(q.y for q in canal[max(0,i-2):i+3])[len(canal[max(0,i-2):i+3])//2])<0.5]
assert len(canal)>=30,len(canal)
smoothed=[sum(canal[max(0,i-1):i+2],Vector())/len(canal[max(0,i-1):i+2]) for i in range(len(canal))]
samples=[smoothed[0]]
for p,q in zip(smoothed,smoothed[1:]):
    steps=max(1,int((q-p).length/0.3))
    samples.extend(p+(q-p)*(i/steps) for i in range(1,steps+1))
conus=next(o for o in skeleton if o.name=='L2');conus_height=sum(v.co.z for v in conus.data.vertices)/len(conus.data.vertices)
cord_vertices=[];cord_faces=[];rings=[];sides=16
for i,p in enumerate(samples):
    tangent=(samples[min(i+1,len(samples)-1)]-samples[max(i-1,0)]).normalized()
    lateral=Vector((1,0,0));depth=tangent.cross(lateral).normalized()
    #the cord narrows to the conus at the second lumbar vertebra, the roots below it run on as a thinner bundle
    taper=0.55+0.45*smoothstep(conus_height-1.5,conus_height+1.5,p.z)
    end=min(i,len(samples)-1-i)*0.3;cap=math.sqrt(max(0,min(1,(end+0.05)/0.5)))
    ring=[]
    for k in range(sides):
        angle=k*math.tau/sides
        ring.append(len(cord_vertices));cord_vertices.append(p+lateral*(math.cos(angle)*0.38*taper*cap)+depth*(math.sin(angle)*0.29*taper*cap))
    rings.append(ring)
for r0,r1 in zip(rings,rings[1:]):
    for k in range(sides):cord_faces.append((r0[k],r1[k],r1[(k+1)%sides],r0[(k+1)%sides]))
for ring,pole,flip in ((rings[0],samples[0]+(samples[0]-samples[1]),False),(rings[-1],samples[-1]+(samples[-1]-samples[-2]),True)):
    index=len(cord_vertices);cord_vertices.append(pole)
    for k in range(sides):
        u,w=ring[k],ring[(k+1)%sides]
        cord_faces.append((index,w,u) if flip else (index,u,w))
cord=mesh_object('spinal cord',[tuple(v) for v in cord_vertices],cord_faces)
cord.data.materials.append(MATERIALS[CORD]);cord['kind']='organ'
organs.append(cord);anatomy.append(cord)
report['cord']={'canal_samples':len(canal),'top':list(samples[0]),'bottom':list(samples[-1])}

#the genitals keep their own shape at the torso's scale, anchored at the pubic symphysis of the registered hip bones,
#then lifted and shrunk only as far as the game's crotch needs to hold them; the vanilla body has no scrotum
pubic=numpy.vstack([numpy.array(donor(f'{side} hip bone')[0]) for side in ('left','right')])
pubic=pubic[numpy.abs(pubic[:,0])<12];donor_anchor=pubic[numpy.argmin(pubic[:,1])].copy();donor_anchor[0]=0
hips=numpy.array([tuple(v.co) for o in skeleton if o.name.endswith('hip bone') for v in o.data.vertices])
hips=hips[numpy.abs(hips[:,0]-game_mid_x)<1];game_anchor=hips[numpy.argmax(hips[:,1])].copy();game_anchor[0]=game_mid_x
for obj in genitals:
    for v in obj.data.vertices:
        d=numpy.array(tuple(v.co))-donor_anchor
        v.co=Vector(game_anchor+numpy.array((-d[0],-d[1],d[2]))*SCALE)
    obj.data.update()
#inside the body on a 0.25 unit grid around the crotch, one cell of clearance
STEP=0.25;lo=numpy.array((game_mid_x-9,-8.0,52.0));shape=(72,96,80)
cells=numpy.zeros(shape,dtype=bool)
for i in range(shape[0]):
    for j in range(shape[1]):
        for k in range(shape[2]):
            cells[i,j,k]=inside(Vector(lo+numpy.array((i,j,k))*STEP))
held=cells.copy()
for axis in range(3):
    held&=numpy.roll(cells,1,axis)&numpy.roll(cells,-1,axis)
points=numpy.array([tuple(v.co) for o in genitals for v in o.data.vertices])
def fits(scale,lift,back):
    moved=game_anchor+(points-game_anchor)*scale+numpy.array((0,back,lift))
    index=numpy.round((moved-lo)/STEP).astype(int)
    if (index<0).any() or (index>=numpy.array(shape)).any():return False
    return bool(held[index[:,0],index[:,1],index[:,2]].all())
placement=None
for scale in numpy.arange(1.0,0.39,-0.05):
    options=[(abs(l)+abs(b),l,b) for l in numpy.arange(0,10.01,0.25) for b in numpy.arange(-6,4.01,0.25) if fits(scale,l,b)]
    if options:
        _,lift,back=min(options);placement=(float(scale),float(lift),float(back));break
assert placement,'the genitals do not fit the crotch at any scale'
scale,lift,back=placement
for obj in genitals:
    for v in obj.data.vertices:v.co=Vector(game_anchor+(numpy.array(tuple(v.co))-game_anchor)*scale+numpy.array((0,back,lift)))
    obj.data.update()
report['genitals']={'donor_anchor':donor_anchor.tolist(),'game_anchor':game_anchor.tolist(),'scale':scale*SCALE,'shrink':scale,'lift':lift,'back':back,
    'outside':{o.name:outside(o) for o in genitals}}
print('GENITALS',json.dumps(report['genitals']),flush=True)
anatomy+=genitals;organs+=genitals
report['outside_skin']={o.name:outside(o) for o in anatomy}
for obj in anatomy:
    for v in obj.data.vertices:
        if not inside(v.co):report.setdefault('outside_points',[]).append([obj.name,round(v.co.x,2),round(v.co.y,2),round(v.co.z,2)])

#each mesh blends between the two bones that move the skin around it most, each vertex by the skin's own weights
tree=KDTree(len(game_vertices))
for i,v in enumerate(game_vertices):tree.insert(v,i)
tree.balance()
def skin_weights(point):
    total=numpy.zeros(len(INFLUENCES))
    for co,index,distance in tree.find_n(point,8):
        total+=game_weights[index]/max(distance,0.05)**2
    return total/max(total.sum(),1e-9)
for obj in anatomy:
    weights=numpy.array([skin_weights(v.co) for v in obj.data.vertices])
    order=numpy.argsort(-weights.mean(0));first,second=int(order[0]),int(order[1])
    if weights.mean(0)[second]<0.02:second=first
    if obj['kind']!='organ':
        mean=weights.mean(0);share=mean[first]/max(mean[first]+mean[second],1e-9)
        binds=[1.0 if second==first else float(share)]*len(weights)
    else:
        binds=[1.0 if second==first else float(w[first]/max(w[first]+w[second],1e-9)) for w in weights]
    obj['first']=first;obj['second']=second;obj['binds']=binds
    report.setdefault('binding',{})[obj.name]={'first':INFLUENCES[first],'second':INFLUENCES[second],'bind_min':min(binds),'bind_max':max(binds)}

if PREVIEW:
    ref=mesh_object('game body',[tuple(v) for v in game_vertices],game_faces)
    ref.display_type='WIRE'
    glass=material('skin',(0.8,0.65,0.55),0.5);ref.data.materials.append(glass)
    glass.blend_method='BLEND' if hasattr(glass,'blend_method') else None
    bsdf=glass.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Alpha'].default_value=0.18
    target=Vector((game_mid_x,1,86))
    for name,location in (('front',(30,90,92)),('side',(95,5,92)),('back',(-25,-90,92))):
        bpy.ops.object.camera_add(location=location)
        camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        camera.data.type='ORTHO';camera.data.ortho_scale=62;bpy.context.scene.camera=camera
        if not any(o.type=='LIGHT' for o in bpy.data.objects):
            for pos,power in (((60,80,140),60000),((-60,-60,100),25000)):
                bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.data.energy=power;light.data.size=60
                light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
        scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.world=bpy.data.worlds.new('world') if not scene.world else scene.world
        scene.world.color=(0.04,)*3;scene.render.resolution_x=900;scene.render.resolution_y=900
        scene.render.filepath=str(ROOT/f'assets/previews/torso_{name}.png');bpy.ops.render.render(write_still=True)
#positions leave the build in rest Spine1 space
to_frame(anatomy,skin_to_bone('Bip01 Spine1'))
export(anatomy,MATERIALS,OUTPUT,report)
#women's torso is the same anatomy without the external genitals
female={'coordinate_space':report['coordinate_space'],'meshes':[]}
export([o for o in anatomy if o not in genitals],MATERIALS,OUTPUT.with_name('torso-female.xra'),female)
report['female']={'bytes':female.get('bytes'),'triangles':female.get('triangles')}
(ROOT/'assets/torso_report.json').write_text(json.dumps(report,indent=2))

#the vanilla body reference stays private, only the donor anatomy is saved
for obj in [o for o in bpy.data.objects if o.name=='game body']:
    mesh=obj.data;bpy.data.objects.remove(obj,do_unlink=True);bpy.data.meshes.remove(mesh)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/torso_anatomy.blend'))
print('TORSO_BUILT',json.dumps({k:v for k,v in report.items() if k in ('outside_skin','organs','cord','bytes','triangles','binding')}),json.dumps({k:v for k,v in report['registration'].items() if k!='levels'}),flush=True)
