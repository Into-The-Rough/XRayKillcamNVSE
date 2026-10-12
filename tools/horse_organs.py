"""Reduce the three horse organ scans into the closed donor meshes the horse build uses, packed as assets/source/horse-organ-donors.zip
with a checksum manifest. The scans are Sketchfab downloads, each a zip holding a source archive:

    blender -b -P tools/horse_organs.py -- <horse-brain.zip> <heart-horse-plastinated.zip> <equine-gi-tract.zip>"""
import bpy
import bmesh
import hashlib
import io
import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'assets/source/extracted/horse-organs'
#name, source file inside the scan's archive, triangle budget, whether its surface has holes a voxel remesh falls through, author, page
ORGANS = [
    ('brain', 'model.obj', 6000, False, 'jc196643', 'https://sketchfab.com/3d-models/horse-brain-f2bb07bb7778424d850c8be09d96bbc8'),
    ('heart', 'Model_01.obj', 4000, False, 'University of Sarajevo - Veterinary faculty',
     'https://sketchfab.com/3d-models/heart-horse-plastinated-fce338c614f44b5aae3ddd38226b97b7'),
    ('gut', 'Equine_lower_GI_mesh.ply', 8000, True, 'auburton2021', 'https://sketchfab.com/3d-models/equine-gi-tract-3692febcfa34423d9bd9b598f4fe8d04'),
]


def unpack(download, wanted):
    """The wanted file out of the source archive nested in a Sketchfab download."""
    with zipfile.ZipFile(download) as outer:
        for name in outer.namelist():
            if not name.endswith('.zip'):
                continue
            with zipfile.ZipFile(io.BytesIO(outer.read(name))) as inner:
                for item in inner.namelist():
                    if Path(item).name == wanted:
                        out = WORK / 'raw' / wanted
                        out.parent.mkdir(parents=True, exist_ok=True)
                        out.write_bytes(inner.read(item))
                        return out
    raise RuntimeError(f'{wanted} missing from {download}')


def apply(obj, mod):
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)


def reduce(path, budget, torn):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if path.suffix == '.obj':
        bpy.ops.wm.obj_import(filepath=str(path), forward_axis='Y', up_axis='Z')
    else:
        bpy.ops.wm.ply_import(filepath=str(path), forward_axis='Y', up_axis='Z')
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if torn:
        #the octree remesh wraps the scan over its holes first
        mod = obj.modifiers.new('reduce', 'DECIMATE'); mod.ratio = 0.05; apply(obj, mod)
        mod = obj.modifiers.new('wrap', 'REMESH'); mod.mode = 'SMOOTH'; mod.octree_depth = 7; mod.scale = 0.9
        mod.use_remove_disconnected = True; mod.threshold = 0.5; apply(obj, mod)
    size = max(max(v.co[i] for v in obj.data.vertices) - min(v.co[i] for v in obj.data.vertices) for i in range(3))
    mod = obj.modifiers.new('close', 'REMESH'); mod.mode = 'VOXEL'; mod.voxel_size = size / (120 if torn else 140); mod.adaptivity = 0
    apply(obj, mod)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    remaining = set(bm.verts)
    islands = []
    while remaining:
        seed = remaining.pop()
        island = [seed]
        pending = [seed]
        while pending:
            for e in pending.pop().link_edges:
                for v in e.verts:
                    if v in remaining:
                        remaining.remove(v); island.append(v); pending.append(v)
        islands.append(island)
    islands.sort(key=len, reverse=True)
    bmesh.ops.delete(bm, geom=[v for i in islands[1:] for v in i], context='VERTS')
    bm.to_mesh(obj.data)
    bm.free()
    triangles = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    mod = obj.modifiers.new('budget', 'DECIMATE'); mod.ratio = min(1, budget / triangles); apply(obj, mod)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    if any(not e.is_manifold for e in bm.edges):
        raise RuntimeError(f'{path.name}: reduced surface still open')
    lines = [f'v {v.co.x:.5f} {v.co.y:.5f} {v.co.z:.5f}' for v in bm.verts]
    index = {v: i + 1 for i, v in enumerate(bm.verts)}
    lines += ['f ' + ' '.join(str(index[v]) for v in f.verts) for f in bm.faces]
    bm.free()
    return ('\n'.join(lines) + '\n').encode(), triangles


def main():
    downloads = sys.argv[sys.argv.index('--') + 1:]
    if len(downloads) != len(ORGANS):
        sys.exit('pass the brain, heart and gut downloads after --')
    manifest = {'license': 'CC BY 4.0', 'note': 'each scan voxel remeshed closed, its largest piece kept and reduced by tools/horse_organs.py',
                'files': []}
    out = ROOT / 'assets/source/horse-organ-donors.zip'
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for (name, wanted, budget, torn, author, page), download in zip(ORGANS, downloads):
            data, before = reduce(unpack(download, wanted), budget, torn)
            path = f'horse/{name}.obj'
            info = zipfile.ZipInfo(path, date_time=(2026, 10, 12, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
            manifest['files'].append({'path': path, 'sha256': hashlib.sha256(data).hexdigest(), 'author': author, 'source': page})
            print('HORSE_ORGAN', name, before, 'triangles after closing', flush=True)
    (ROOT / 'assets/source/horse-organ-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


main()
