import bpy
from pathlib import Path
path=Path(__file__).resolve().parents[1]/'assets/head_anatomy.blend'
bpy.ops.wm.open_mainfile(filepath=str(path),load_ui=False)
assert not any('reference' in o.name.lower() for o in bpy.data.objects)
assert not any(o.name in ('HeadMale','FemaleHead') for o in bpy.data.objects)
print('SCENE_OK',len(bpy.data.objects),'objects; no game reference geometry',flush=True)
