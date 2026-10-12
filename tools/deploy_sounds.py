"""Fills the mod's gore sound folders from the audition picks.

The picks file lists clip ids per moment (hit, bone, organ, burst, spray). jse: clips come from a folder of Gore Mini Pack
clips split by id, fnv: clips from the game's own archives through the audition manifest. Every wav and ogg already in the
five folders is replaced."""
import argparse, json, shutil, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from bsa import Archive

MOMENTS = ['hit', 'bone', 'organ', 'burst', 'spray']
parser = argparse.ArgumentParser()
parser.add_argument('picks', type=Path)
parser.add_argument('--mod', type=Path, required=True)
parser.add_argument('--pack', type=Path, required=True)
parser.add_argument('--manifest', type=Path, required=True)
parser.add_argument('--data', type=Path, required=True)
args = parser.parse_args()

picks = json.loads(args.picks.read_text())
paths = {c['id']: c['path'] for c in json.loads(args.manifest.read_text())['clips'] if 'path' in c}
archives = {}
def game_clip(path):
    for name in ('Fallout - Sound.bsa', 'HonestHearts - Sounds.bsa'):
        if name not in archives: archives[name] = Archive(str(args.data / name))
        if path.lower() in archives[name].entries: return archives[name].read(path)
    raise KeyError(path)

for moment in MOMENTS:
    folder = args.mod / 'sound' / 'fx' / 'xraykillcam' / moment
    folder.mkdir(parents=True, exist_ok=True)
    for old in list(folder.glob('*.wav')) + list(folder.glob('*.ogg')): old.unlink()
    for clip in picks.get(moment, []):
        kind, name = clip.split(':', 1)
        if kind == 'jse':
            shutil.copyfile(args.pack / name.split('_', 1)[0] / (name + '.wav'), folder / (name + '.wav'))
        elif kind == 'fnv':
            (folder / (name + '.wav')).write_bytes(game_clip(paths[clip]))
        else:
            raise ValueError(clip)
    print(moment, len(picks.get(moment, [])), 'clips ->', folder)
