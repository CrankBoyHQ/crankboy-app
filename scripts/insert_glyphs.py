#!/usr/bin/env python3
# add glyphs from a ttf into a playdate .fnt + table png pair.
#  usage: insert_glyphs.py <glyphs.txt> <font.ttf> <table-w-h.png> <font.fnt> [--beta-gumi=N]

import os
import re
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

THRESHOLD = 128

def parse_cell_size(png_path):
    m = re.search(r"-(\d+)-(\d+)\.png$", png_path)
    if not m:
        raise ValueError("png name must end in -W-H.png")
    return int(m.group(1)), int(m.group(2))


def classify(line):
    if line == "":
        return "blank"
    if "\t" not in line:
        return "header"
    name = line.split("\t")[0]
    if name == "space" or len(name) == 1:
        return "glyph"
    return "kern"

def is_halfwidth_kana(ch):
    # U+FF61..U+FF9F: halfwidth katakana, voiced marks, punctuation
    return 0xFF61 <= ord(ch) <= 0xFF9F

def beta_advance(ch, beta):
    # halfwidth kana get half the beta-gumi advance, else they waste
    # a full-width cell and defeat their purpose
    return (beta + 1) // 2 if is_halfwidth_kana(ch) else beta

def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    beta = None
    for a in argv[1:]:
        if a.startswith("--beta-gumi="):
            beta = int(a.split("=", 1)[1])
    if len(args) != 4:
        sys.stderr.write(__doc__)
        return 2
    glyphs_txt, ttf_path, png_path, fnt_path = args

    cell_w, cell_h = parse_cell_size(png_path)
    wanted = [l for l in open(glyphs_txt, encoding="utf-8").read().split("\n") if l]

    cmap = TTFont(ttf_path).getBestCmap() # skip glyphs the font lacks
    lines = open(fnt_path, encoding="utf-8").read().split("\n")
    kinds = [classify(l) for l in lines]
    glyph_idx = [i for i, k in enumerate(kinds) if k == "glyph"]
    have = set()
    for i in glyph_idx:
        name = lines[i].split("\t")[0]
        have.add(" " if name == "space" else name)

    todo = [g for g in wanted if g not in have and len(g) == 1 and ord(g) in cmap]
    if not todo:
        print("nothing to add to %s" % os.path.basename(fnt_path))
        return 0

    size = beta if beta else cell_h
    font = ImageFont.truetype(ttf_path, size)
    ascent, descent = font.getmetrics()

    img = Image.open(png_path).convert("RGBA")
    cols = img.width // cell_w
    start = len(glyph_idx)
    new_lines = []

    # Baseline row within a cell, measured from the host font so inserted
    # glyphs share the exact baseline of the existing (e.g. romaji) glyphs.
    # 'H' sits flat on the baseline; fall back to donor metrics if absent.
    base_row = None
    for ref in ("H", "A", "x"):
        for j, li in enumerate(glyph_idx):
            if lines[li].split("\t")[0] == ref:
                rcx, rcy = (j % cols) * cell_w, (j // cols) * cell_h
                for yy in range(rcy + cell_h - 1, rcy - 1, -1):
                    if any(
                        img.getpixel((xx, yy))[3] >= THRESHOLD
                        for xx in range(rcx, rcx + cell_w)
                    ):
                        base_row = yy - rcy + 1
                        break
            if base_row is not None:
                break
        if base_row is not None:
            break
    if base_row is None:
        base_row = int((cell_h - (ascent + descent)) / 2 + ascent + 0.5)

    rows_needed = (start + len(todo) + cols - 1) // cols
    if rows_needed * cell_h > img.height:
        grown = Image.new("RGBA", (img.width, rows_needed * cell_h), (0, 0, 0, 0))
        grown.paste(img, (0, 0))
        img = grown

    px = img.load()
    pad = size * 3
    for i, ch in enumerate(todo):
        idx = start + i
        cx, cy = (idx % cols) * cell_w, (idx // cols) * cell_h

        scratch = Image.new("L", (pad, pad), 0)
        d = ImageDraw.Draw(scratch)
        oy = pad // 2
        d.text((pad // 4, oy), ch, font=font, fill=255, anchor="ls")
        bbox = scratch.getbbox()

        if bbox is None:
            new_lines.append("%s\t%d" % (ch, beta_advance(ch, beta) if beta else size))
            continue
        il, it, ir, ib = bbox
        ink_w = ir - il
        # never narrower than the ink, or the left edge would clip
        advance = max(beta_advance(ch, beta), ink_w) if beta else ink_w
        # halfwidth kana keep left bearing so dakuten (ﾞﾟ) stay attached
        bearing = (advance - ink_w) // 2 if beta and not is_halfwidth_kana(ch) else 0

        dst_x = cx + bearing
        # integer math only; a fractional base rounded per-glyph caused 1px
        # baseline jitter between glyphs
        dst_y = cy + base_row - (oy - it)

        # Snap near-baseline ink bottoms onto the host baseline row: the donor
        # rasterizer wobbles ±1px per glyph at small sizes (e.g. レ vs ク).
        # Use thresholded ink rows, not the raw bbox (it includes faint pixels).
        # Marks (dakuten, prolonged-sound) sit far above and stay untouched.
        vis_bottom = max(
            y
            for y in range(it, ib)
            if any(scratch.getpixel((x, y)) >= THRESHOLD for x in range(il, ir))
        )
        host_bottom = cy + base_row - 1
        cell_bottom = dst_y + (vis_bottom - it)
        if abs(cell_bottom - host_bottom) <= 1:
            dst_y += host_bottom - cell_bottom
        for y in range(it, ib):
            for x in range(il, ir):
                if scratch.getpixel((x, y)) >= THRESHOLD:
                    tx, ty = dst_x + (x - il), dst_y + (y - it)
                    if cx <= tx < cx + cell_w and cy <= ty < cy + cell_h:
                        px[tx, ty] = (0, 0, 0, 255)
        new_lines.append("%s\t%d" % (ch, advance))

    img.save(png_path)
    at = glyph_idx[-1] + 1
    lines[at:at] = new_lines
    with open(fnt_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("added %d glyphs to %s" % (len(new_lines), os.path.basename(fnt_path)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
