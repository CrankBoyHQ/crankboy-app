#!/usr/bin/env python3
# gets all ja glyphs used in jp.strings
# usage: list_jp_glyphs.py <strings-file> <out.txt>

import sys
import unicodedata

HIRAGANA = list("ぁあぃいぅうぇえぉおかがきぎくぐけげこごさざしじすずせぜそぞただちぢっつづてでとどなにぬねのはばぱひびぴふぶぷへべぺほぼぽまみむめもゃやゅゆょよらりるれろゎわゐゑをんゔゕゖ")
KATAKANA = list("ァアィイゥウェエォオカガキギクグケゲコゴサザシジスズセゼソゾタダチヂッツヅテデトドナニヌネノハバパヒビピフブプヘベペホボポマミムメモャヤュユョヨラリルレロヮワヰヱヲンヴヵヶヷヸヹヺー")
PUNCT = list("　、。，．・：；！？「」『』（）〈〉《》【】〔〕〜…‥々＝＋－％＆＃＠～")
GLYPHS = HIRAGANA + KATAKANA + PUNCT


def scan_strings(path):
    out = set()
    for ch in open(path, encoding="utf-8").read():
        # kanji, fullwidth latin, etc.
        if ord(ch) >= 0x3000:
            out.add(ch)
    return out

TENTEN = {0x3099: "゙", 0x309A: "゚", 0x309B: "゙", 0x309C: "゚"}

def stray_tenten(text):
    # should be baked in
    bad = []
    for i, ch in enumerate(text):
        combining = TENTEN.get(ord(ch))
        if combining and i > 0:
            baked = unicodedata.normalize("NFC", text[i - 1] + combining)
            if len(baked) == 1:
                bad.append((text[i - 1], ch, baked))
    return bad


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    text = open(argv[1], encoding="utf-8").read()
    bad = stray_tenten(text)
    if bad:
        for base, mark, baked in bad:
            sys.stderr.write("error: %s + U+%04X must be baked as %s\n"
                             % (base, ord(mark), baked))
        return 1
    glyphs = set(GLYPHS) | scan_strings(argv[1])
    ordered = sorted(glyphs, key=ord)
    with open(argv[2], "w", encoding="utf-8") as f:
        f.write("\n".join(ordered) + "\n")
    print("wrote %d glyphs to %s" % (len(ordered), argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
