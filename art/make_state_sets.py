"""Make the _drain (pink) and _death (red) template sets from the base art by recolouring the
lilac fill, then export all three sets as uncompressed DDS with mips into the package.
Usage: make_state_sets.py <texconv.exe> <pkg_root>"""
import os, sys, subprocess, tempfile, shutil
from PIL import Image

texconv, pkg = sys.argv[1], sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))

# lilac (base) -> pink (drain) / red (death): the palette the DLL uses for tinting, applied exactly.
STATES = {
    '_drain': ((255, 207, 242), (255, 157, 227)),
    '_death': ((255, 102, 102), (255, 51, 51)),
}
BASE = ((235, 200, 255), (165, 90, 255))   # top / bottom of the base fill gradient

def recolour(img, light, dark):
    """Pixels that are 'lilac fill' get remapped onto the new light->dark gradient by luminance."""
    im = img.convert('RGBA')
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            # fill detection: blue-dominant, not the dark trough, not the near-white frame
            if b > 200 and r > 120 and not (r > 235 and g > 225):
                t = (235 - r) / 70.0  # 0 at the top of the gradient, 1 at the bottom
                t = max(0.0, min(1.0, t))
                nr = int(light[0] + (dark[0] - light[0]) * t)
                ng = int(light[1] + (dark[1] - light[1]) * t)
                nb = int(light[2] + (dark[2] - light[2]) * t)
                px[x, y] = (nr, ng, nb, a)
    return im

tmp = tempfile.mkdtemp(prefix='lilith_states_')
jobs = []  # (png path, dest dir)
for i in range(9):
    src = os.path.join(here, 'stages', 'energy%d.png' % i)
    for suf, (light, dark) in STATES.items():
        out = os.path.join(tmp, 'energy%d%s.png' % (i, suf))
        recolour(Image.open(src), light, dark).save(out)
        jobs.append((out, os.path.join(pkg, 'Art', 'Stages')))
src = os.path.join(here, 'bar_fill.png')
for suf, (light, dark) in STATES.items():
    out = os.path.join(tmp, 'bar_fill%s.png' % suf)
    recolour(Image.open(src), light, dark).save(out)
    jobs.append((out, os.path.join(pkg, 'Art', 'Crop')))

for png, dest in jobs:
    os.makedirs(dest, exist_ok=True)
    subprocess.run([texconv, '-nologo', '-y', '-f', 'B8G8R8A8_UNORM', '-m', '0', '-o', dest, png], check=True, capture_output=True)
    shutil.copy(png, os.path.join(here, 'stages' if 'energy' in png else '', os.path.basename(png)))
print('wrote', len(jobs), 'state textures')
shutil.rmtree(tmp, ignore_errors=True)
