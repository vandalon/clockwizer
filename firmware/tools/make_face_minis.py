#!/usr/bin/env python3
"""Makes the little pictures of the clock faces for the settings page: each thumbnail in docs/images/faces/ is a photo
of the real panel, which this reduces to a 32x32 grid of 12 colours and stores in docs/faces.json as "icon".
Run it by hand when a thumbnail changes (needs Pillow and numpy), then commit faces.json:

    python3 tools/make_face_minis.py

The "icon" holds the colours as "rrggbb" pairs and the pixels as base64 of raw deflate (one byte per pixel, the
index of its colour, row by row). LEDs that are off in the photo are made black first, which is what makes it
compress. A face without a thumbnail gets no icon.
"""
import base64, json, os, zlib
import numpy as np
from PIL import Image

SIZE = 32   # the panel is 64x64 LEDs, so every icon pixel is 2x2 LEDs
COLORS = 12
OFF = 45    # an LED dimmer than this (of 255) counts as off
LEDS = 64
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs")


def led_grid(path):
    """The colour of each LED of the photographed panel: the middle of its cell, brightened like the real thing."""
    photo = np.asarray(Image.open(path).convert("RGB").resize((LEDS * 10, LEDS * 10), Image.LANCZOS)).astype(float)
    leds = np.zeros((LEDS, LEDS, 3))
    for y in range(LEDS):
        for x in range(LEDS):
            leds[y, x] = photo[y * 10 + 2:y * 10 + 8, x * 10 + 2:x * 10 + 8].reshape(-1, 3).mean(0)
    peak = np.percentile(leds.max(2), 99.5)
    return np.clip((leds / peak) ** 0.8 * 255, 0, 255)


def make_icon(path):
    k = LEDS // SIZE
    small = led_grid(path).reshape(SIZE, k, SIZE, k, 3).mean((1, 3))
    small[small.max(2) < OFF] = 0  # the photo's noise on unlit LEDs would break up every run of black
    q = Image.fromarray(small.astype("uint8")).quantize(COLORS, method=Image.MEDIANCUT, dither=Image.NONE)
    palette = q.getpalette()[:COLORS * 3]
    deflate = zlib.compressobj(9, zlib.DEFLATED, -15)  # raw deflate: what DecompressionStream("deflate-raw") reads
    packed = deflate.compress(bytes(q.getdata())) + deflate.flush()
    return {"size": SIZE, "palette": "".join("%02x" % c for c in palette), "pixels": base64.b64encode(packed).decode()}


def main():
    faces_path = os.path.join(ROOT, "faces.json")
    faces = json.load(open(faces_path))
    for face in faces:
        thumb = face.get("thumb")
        if thumb and os.path.exists(os.path.join(ROOT, thumb)):
            face["icon"] = make_icon(os.path.join(ROOT, thumb))
        else:
            face.pop("icon", None)
    # one face per line, as it was written by hand
    open(faces_path, "w").write("[\n" + ",\n".join("  " + json.dumps(f, ensure_ascii=False) for f in faces) + "\n]\n")


if __name__ == "__main__":
    main()
