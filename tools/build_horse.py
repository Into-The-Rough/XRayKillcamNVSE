"""Export the fitted horse skeleton as the four horse anatomy assets, each in its region's frame bone rest space.

Run tools/horse_fit.py first, then:
    blender -b -P tools/build_horse.py [-- --preview]"""
import bpy
import bmesh
import json
import sys
import numpy
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from anatomy_build import *

PREVIEW='--preview' in sys.argv
bpy.ops.wm.read_factory_settings(use_empty=True)
fitted=json.loads((ROOT/'assets/source/extracted/horse/fitted.json').read_text())
rest={n:numpy.array(m) for n,m in fitted['rest'].items()}
MATERIALS=make_materials()
MATERIAL_IDS={'bone':BONE,'teeth':3,'cartilage':CARTILAGE,'brain':CORD,'heart':HEART,'gut':GUT}
#the opening the brainstem leaves the skull through, in game units
FORAMEN=0.8
#runtime influence tables in kHorseSpecs order, index 0 is the frame bone itself
REGIONS={
    'head':('horse-head.xra',('Bip01 Head','Bip01 Jaw','Bip01 Neck2','Bip01 Neck1','Bip01 Neck')),
    'torso':('horse-torso.xra',('Bip01 Spine1','Bip01 Spine2','Bip01 Pelvis','Bip01 L Clavicle','Bip01 R Clavicle','Bip01 L Thigh','Bip01 R Thigh')),
    'legs':('horse-legs.xra',('Bip01 Pelvis','Bip01 L Calf','Bip01 L HorseLink','Bip01 L Foot','Bip01 R Calf','Bip01 R HorseLink','Bip01 R Foot')),
    'arms':('horse-arms.xra',('Bip01 Spine2','Bip01 L UpperArm','Bip01 L Forearm','Bip01 L Hand','Bip01 R UpperArm','Bip01 R Forearm','Bip01 R Hand')),
}


def closed_shell(name,shell):
    obj=mesh_object(name,[tuple(v) for v in shell['vertices']],[tuple(f) for f in shell['faces']])
    if open_edges(obj):
        #a small gap in the sculpt is filled, a torn surface is rebuilt
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.holes_fill(bm,edges=[e for e in bm.edges if e.is_boundary],sides=0)
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        bm.to_mesh(obj.data);bm.free();obj.data.update();tidy(obj)
        if open_edges(obj):
            remesh(obj,0.25);keep_largest(obj,0.05);reduce(obj,int(len(shell['faces'])*1.2));tidy(obj)
        if open_edges(obj):raise RuntimeError(f'{name}: surface still open after repair')
        report.setdefault('repaired',[]).append(name)
    return obj


def carve(obj,cavity):
    #the skull is solid, the brain's room is cut out of it a little larger than the brain, opening out of the back of the skull
    #through the foramen magnum along the brainstem, so the cavity's wall is part of the skull's surface
    v=numpy.array(cavity['vertices']);c=v.mean(0)
    stem=numpy.array(cavity['stem']);stem/=numpy.linalg.norm(stem)
    bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=FORAMEN,depth=24,location=tuple(c+stem*12))
    channel=bpy.context.object;channel.rotation_euler=Vector(stem).to_track_quat('Z','Y').to_euler()
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    room=mesh_object('cavity',[tuple(c+(p-c)*1.06) for p in v],[tuple(f) for f in cavity['faces']])
    for cutter in (channel,room):
        mod=obj.modifiers.new('cavity','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
        apply_modifier(obj,mod);bpy.data.objects.remove(cutter,do_unlink=True)
    tidy(obj)
    if open_edges(obj):raise RuntimeError(f'{obj.name}: open after carving the brain cavity')


report={'meshes':[]}
summary={}
brain=next(s for s in fitted['shells'] if s['label']=='brain')
for region,(file,influences) in REGIONS.items():
    report={'coordinate_space':f'{influences[0]} rest local, game units','meshes':[]}
    groups={}
    for n,shell in enumerate(fitted['shells']):
        if shell['region']!=region:continue
        obj=closed_shell(f"{shell['label']} {n}",shell)
        if shell['label']=='skull':carve(obj,brain)
        groups.setdefault((shell['material'],shell['bone']),[]).append(obj)
    anatomy=[]
    for (material_name,bone),objects in sorted(groups.items()):
        bpy.ops.object.select_all(action='DESELECT')
        for obj in objects:obj.select_set(True)
        bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.join()
        obj=objects[0];obj.name=f'{material_name} {bone}'
        obj.data.materials.clear();obj.data.materials.append(MATERIALS[MATERIAL_IDS[material_name]])
        obj['first']=obj['second']=influences.index(bone);obj['binds']=[1.0]*len(obj.data.vertices)
        anatomy.append(obj)
    #positions leave the build in the frame bone's rest space
    to_frame(anatomy,numpy.linalg.inv(rest[influences[0]]).T)
    export(anatomy,MATERIALS,ROOT/'package/meshes/XRayKillcamNVSE'/file,report)
    (ROOT/f'assets/horse_{region}_report.json').write_text(json.dumps(report,indent=2))
    summary[region]={'meshes':len(report['meshes']),'triangles':report['triangles'],'bytes':report['bytes'],'repaired':report.get('repaired',[])}
    if PREVIEW:
        for obj in anatomy:
            for v in obj.data.vertices:
                p=rest[influences[0]]@numpy.array([*v.co,1.0]);v.co=Vector(p[:3])
            obj.data.update()
    else:
        for obj in anatomy:
            mesh=obj.data;bpy.data.objects.remove(obj,do_unlink=True);bpy.data.meshes.remove(mesh)

if PREVIEW:
    body=mesh_object('horse body',[tuple(v) for v in fitted['body']['vertices']],[tuple(t) for t in fitted['body']['triangles']])
    glass=material('skin',(0.8,0.65,0.55),0.5);body.data.materials.append(glass)
    glass.node_tree.nodes.get('Principled BSDF').inputs['Alpha'].default_value=0.15
    target=Vector((0,20,70))
    for name,location in (('side',(260,20,75)),('front',(150,260,110)),('top',(0,20,330))):
        bpy.ops.object.camera_add(location=location)
        camera=bpy.context.object;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        camera.data.type='ORTHO';camera.data.ortho_scale=190;bpy.context.scene.camera=camera
        if not any(o.type=='LIGHT' for o in bpy.data.objects):
            for pos,power in (((150,150,250),300000),((-150,-100,120),120000)):
                bpy.ops.object.light_add(type='AREA',location=pos);light=bpy.context.object;light.data.energy=power;light.data.size=150
                light.rotation_euler=(target-light.location).to_track_quat('-Z','Y').to_euler()
        scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.world=scene.world or bpy.data.worlds.new('world')
        scene.world.color=(0.04,)*3;scene.render.resolution_x=1200;scene.render.resolution_y=1000
        scene.render.filepath=str(ROOT/f'assets/previews/horse_{name}.png');bpy.ops.render.render(write_still=True)
print('HORSE_BUILT',json.dumps(summary),flush=True)
