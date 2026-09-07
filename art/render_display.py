"""Render a transparent PNG mock of the Lilith Widget for the Nexus page, using the shipped
template art (icon + stage frame) laid out the way HudUI.cpp draws it. Usage: render_display.py <out.png>"""
import sys, os
from PIL import Image, ImageDraw, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, 'LilithWidget_display.png')

energy, energy_max, level, ratio = 62, 100, 4, 0.58
S = 2  # supersample for clean edges
bar_w, bar_h = 1024 * S, 96 * S
gap = 10 * S
icon_sz = int(bar_h * 1.5)
xp_h = 14 * S
font_px = 60 * S

stage = min(8, energy * 100 // energy_max // 12) if energy < energy_max else 8
bar = Image.open(os.path.join(here, 'stages', 'energy%d.png' % stage)).convert('RGBA').resize((bar_w, bar_h), Image.LANCZOS)
icon = Image.open(os.path.join(here, 'icon.png')).convert('RGBA').resize((icon_sz, icon_sz), Image.LANCZOS)

font = ImageFont.truetype(r'C:\Windows\Fonts\arialbd.ttf', font_px)
lvl_txt = 'Lv %d' % level
tmp = ImageDraw.Draw(Image.new('RGBA', (10, 10)))
lw = tmp.textbbox((0, 0), lvl_txt, font=font)[2]

W = icon_sz + gap + bar_w + gap + lw + 8 * S
bars_h = bar_h + gap + xp_h
H = max(icon_sz, bars_h) + 8 * S
img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

x = 4 * S
iy = (H - icon_sz) // 2
img.alpha_composite(icon, (x, iy))
x += icon_sz + gap
bar_top = (H - bars_h) // 2
img.alpha_composite(bar, (x, bar_top))

def shadow_text(pos, text, fill):
    d.text((pos[0] + 2 * S, pos[1] + 2 * S), text, font=font, fill=(0, 0, 0, 210))
    d.text(pos, text, font=font, fill=fill)

txt = '%d / %d' % (energy, energy_max)
bb = d.textbbox((0, 0), txt, font=font)
shadow_text((x + (bar_w - bb[2]) // 2, bar_top + (bar_h - bb[3]) // 2 - bb[1] // 2), txt, (255, 255, 255, 255))

# XP strip under the bar
x0, y0 = x, bar_top + bar_h + gap
r = xp_h // 2
d.rounded_rectangle((x0, y0, x0 + bar_w, y0 + xp_h), r, fill=(0, 0, 0, 153))
d.rounded_rectangle((x0, y0, x0 + int(bar_w * ratio), y0 + xp_h), r, fill=(255, 205, 90, 255))

# level label right of the bar
lb = d.textbbox((0, 0), lvl_txt, font=font)
shadow_text((x + bar_w + gap, bar_top + (bar_h - lb[3]) // 2 - lb[1] // 2), lvl_txt, (255, 225, 160, 255))

img = img.resize((W // S, H // S), Image.LANCZOS)
img.save(out)
print('wrote', out, img.size)
