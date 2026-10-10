# Adds the accented letters the Time in Words languages use (clockfaces/cw-cf-0x02/WordClock.cpp)
# to that face's three fonts: each one is the plain letter with its mark drawn above, in the
# font's own stroke width. They go after '~', in the order of WordGlyphs.h, which the face
# uses to turn UTF-8 into the font's codes. Run it again after changing the words:
#
#   python3 tools/word_fonts.py
import os, re, sys, unicodedata

FACE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "clockfaces", "cw-cf-0x02")
FONTS = {"hour8pt7b": 2, "minute7pt7b": 1, "small4pt7b": 1}  # name: stroke width of its marks

# The marks, top row first, in thin (1 pixel) and thick (2 pixel) strokes
MARKS = {
    "acute": ([".#", "#."], [".##", "##."]),
    "grave": (["#.", ".#"], ["##.", ".##"]),
    "circumflex": ([".#.", "#.#"], [".##.", "#..#"]),
    "caron": (["#.#", ".#."], ["#..#", ".##."]),
    "ring": ([".#.", "#.#", ".#."], [".##.", "#..#", ".##."]),
    "tilde": ([".#.#", "#.#."], [".#.#", "#.#."]),
    "dot": (["#"], ["##", "##"]),
    "breve": (["#..#", ".##."], ["#...#", ".###."]),
    "doubleacute": ([".#.#", "#.#."], ["..#..#", ".#..#.", "#..#.."]),
    "cedilla": ([".#", "##"], [".##", "##."]),  # under the letter, like the two below
    "comma": (["#", "#"], ["##", ".#"]),
    "ogonek": (["#.", ".#"], ["##.", ".##"]),  # at the right foot of the letter
}
BELOW = ("cedilla", "comma", "ogonek")
COMBINING = {"0301": "acute", "0300": "grave", "0302": "circumflex", "030C": "caron", "030A": "ring",
             "0303": "tilde", "0307": "dot", "0308": "diaeresis", "0327": "cedilla",
             "0306": "breve", "030B": "doubleacute", "0326": "comma", "0328": "ogonek"}


def load(name):
    text = open(os.path.join(FACE, name + ".h")).read()
    bits = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", re.search(r"Bitmaps\[\][^{]*\{(.*?)\};", text, re.S).group(1))]
    rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}",
                      re.search(r"Glyphs\[\][^{]*\{(.*?)\};", text, re.S).group(1))
    y_advance = int(re.search(r"0x20,\s*0x[0-9a-fA-F]+,\s*(\d+)\s*\}", text).group(1))
    glyphs = []
    for off, w, h, xa, xo, yo in [tuple(map(int, r)) for r in rows[:95]]:  # ' ' to '~': added ones are made again
        pixels, bit = set(), off * 8
        for y in range(h):
            for x in range(w):
                if bits[bit >> 3] & (0x80 >> (bit & 7)):
                    pixels.add((x + xo, y + yo))
                bit += 1
        glyphs.append({"pixels": pixels, "xa": xa, "box": (w, h, xo, yo)})
    return glyphs, y_advance


def compose(glyphs, ch, thick):
    if ch in "øØ":
        base, mark = ("o" if ch == "ø" else "O"), "slash"
    elif ch in "łŁ":
        base, mark = ("l" if ch == "ł" else "L"), "stroke"
    else:
        parts = unicodedata.decomposition(ch).split()
        if len(parts) != 2 or parts[1] not in COMBINING:
            sys.exit("No way to draw %r yet: add it to tools/word_fonts.py" % ch)
        base, mark = chr(int(parts[0], 16)), COMBINING[parts[1]]
    g = glyphs[ord(base) - 32]
    pixels = set(g["pixels"])
    if base in "ij":  # the dot makes way for the mark
        top = min(y for _, y in pixels)
        ys = sorted({y for _, y in pixels})
        gap = next((y for y in range(top, top + 6) if y not in ys), None)
        if gap is not None:
            pixels = {(x, y) for x, y in pixels if y > gap}
    xs, ys = [x for x, _ in pixels], [y for _, y in pixels]
    left, right, top, bottom = min(xs), max(xs), min(ys), max(ys)
    width = right - left + 1
    if mark == "slash":
        h = bottom - top
        for i in range(h + 1):
            pixels.add((left + round((h - i) * (width - 1) / h), top + i))
    elif mark == "stroke":  # a short slash through the stem, half way up
        stem = min(x for x, y in pixels if y == top)
        mid = (top + bottom + 1) // 2
        reach = 2 if thick else 1
        for i in range(-reach, reach + 1):
            for t in range(2 if thick else 1):
                pixels.add((stem + t + i, mid - i))
    else:
        if mark == "diaeresis":
            s = 2 if thick else 1
            gapx = 1 if width >= 5 else 0
            shape = ["#" * s + "." * (width - 2 * s - 2 * gapx) + "#" * s] * s
            x0 = left + gapx
        else:
            shape = MARKS[mark][1 if thick else 0]
            x0 = right - len(shape[0]) + 1 if mark == "ogonek" else left + (width - len(shape[0]) + 1) // 2
        # one blank row between letter and mark; the ring sits on the letter, or å gets too tall
        y0 = bottom + 1 if mark in BELOW else top - len(shape) - (0 if mark == "ring" else 1)
        for dy, row in enumerate(shape):
            for dx, c in enumerate(row):
                if c == "#":
                    pixels.add((x0 + dx, y0 + dy))
    return {"pixels": pixels, "xa": g["xa"]}


def write(name, glyphs, extra, y_advance):
    data, table = [], []
    for code, g in enumerate(glyphs, 32):
        px = g["pixels"]
        if px:
            xs, ys = [x for x, _ in px], [y for _, y in px]
            xo, yo, w, h = min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        else:  # the space keeps the box it had
            w, h, xo, yo = g.get("box", (0, 0, 0, 0))
        bits = [1 if (xo + x, yo + y) in px else 0 for y in range(h) for x in range(w)]
        offset = len(data)
        for i in range(0, len(bits), 8):
            chunk = bits[i:i + 8] + [0] * (8 - len(bits[i:i + 8]))
            data.append(sum(b << (7 - j) for j, b in enumerate(chunk)))
        if not bits:
            data.append(0)
        label = chr(code) if code < 127 else extra[code - 127]
        label = "backslash" if label == "\\" else "'%s'" % label  # a comment can't end with a backslash
        comma = "," if code - 32 < len(glyphs) - 1 else " "
        table.append("  {%6d, %3d, %3d, %3d, %4d, %4d }%s  // %s" % (offset, w, h, g["xa"], xo, yo, comma, label))
    out = ["#include <Arduino.h>", "#include <gfxfont.h>", "",
           "// The accented letters after '~' are added by tools/word_fonts.py, in the order of WordGlyphs.h.",
           "const uint8_t %sBitmaps[] PROGMEM = {" % name]
    for i in range(0, len(data), 12):
        out.append("  " + ", ".join("0x%02X" % b for b in data[i:i + 12]) + ",")
    out += ["};", "", "const GFXglyph %sGlyphs[] PROGMEM = {" % name, "\n".join(table), "};", "",
            "const GFXfont %s PROGMEM = {" % name,
            "  (uint8_t  *)%sBitmaps," % name,
            "  (GFXglyph *)%sGlyphs, 0x20, 0x%02x, %d };" % (name, 0x7E + len(extra), y_advance), "",
            "// Approx. %d bytes" % (len(data) + 7 * len(glyphs) + 7), ""]
    open(os.path.join(FACE, name + ".h"), "w").write("\n".join(out))


words = open(os.path.join(FACE, "WordClock.cpp"), encoding="utf-8").read()
extra = sorted({c for c in words if ord(c) > 126})
if len(extra) > 128:
    sys.exit("Too many accented letters for one font")
for name, stroke in FONTS.items():
    glyphs, y_advance = load(name)
    write(name, glyphs + [compose(glyphs, c, stroke == 2) for c in extra], extra, y_advance)

codes = ", ".join("0x%04X" % ord(c) for c in extra)
open(os.path.join(FACE, "WordGlyphs.h"), "w").write(
    "#pragma once\n\n#include <stdint.h>\n\n"
    "// Generated by tools/word_fonts.py from the letters WordClock.cpp uses: do not edit.\n"
    "// The accented letters in the fonts, from code 0x7F on: %s\n"
    "static const uint16_t WORD_GLYPHS[] = {%s};\n"
    "static const uint8_t WORD_GLYPH_COUNT = %d;\n" % (" ".join(extra), codes, len(extra)))
print("Added %d letters: %s" % (len(extra), " ".join(extra)))
