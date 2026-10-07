# Turns the preview's PPM pictures into LED panel looking PNGs: one per screen and a sheet of all
import glob, os, sys
from PIL import Image, ImageDraw

SRC, DST = sys.argv[1], sys.argv[2]
S = 8

def panel(path):
    img = Image.open(path).convert("RGB")
    out = Image.new("RGB", (64 * S, 64 * S), (8, 8, 10))
    d = ImageDraw.Draw(out)
    for y in range(64):
        for x in range(64):
            c = img.getpixel((x, y))
            c = c if any(c) else (22, 22, 24)  # an LED that's off
            d.ellipse((x * S + 1, y * S + 1, x * S + S - 2, y * S + S - 2), fill=c)
    return out

NAMES = {
    "s00_start": "start (no matches yet)",
    "s01": "1 live match (minute shown by t)", "s02": "2 live match + two results",
    "s03": "3 three live + three results", "s04": "4 three live, no results", "s05": "5 live 12-1 at half time",
    "s06": "6 seven results", "s07": "7 three results + tonight", "s08": "8 next kick-offs",
    "s09": "9 nothing on (green dot)", "s10": "10 no data yet (amber dot)", "s11": "11 download failed (red dot)",
}

shots = sorted(glob.glob(SRC + "/s*.ppm"))
tiles = []
for p in shots:
    key = os.path.basename(p)[:-4]
    img = panel(p)
    img.save(DST + "/" + key + ".png")
    label = NAMES.get(key.split("_")[0], key)
    t = key.split("_t")[1] if "_t" in key else ""
    tiles.append((img, label + (("  t=%.1fs" % (int(t) / 10)) if t else "")))

goals = sorted(glob.glob(SRC + "/goal_*.ppm"))
if goals:
    pick = [goals[i] for i in (4, 14, 22, 30, 45, len(goals) - 1) if i < len(goals)]
    for i, p in enumerate(pick):
        tiles.append((panel(p), "goal celebration, frame %s" % os.path.basename(p)[5:-4]))
    for p in goals:
        panel(p).save(DST + "/" + os.path.basename(p)[:-4] + ".png")

cols = 4
W = 64 * S
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (W + 12), rows * (W + 34)), (12, 12, 14))
d = ImageDraw.Draw(sheet)
for i, (img, label) in enumerate(tiles):
    x, y = (i % cols) * (W + 12), (i // cols) * (W + 34)
    sheet.paste(img, (x, y))
    d.text((x + 4, y + W + 8), label, fill=(230, 230, 230))
sheet.save(DST + "/sheet.png")
