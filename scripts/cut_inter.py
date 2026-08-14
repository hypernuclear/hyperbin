"""Cut resources/Inter-SemiBold.ttf from Inter's variable font.

The shipped file is a static instance, not the variable font: pinning the
axes here means the app never depends on how a given Qt build interpolates
`wght`, and dropping everything outside Latin takes it from 875 KB to 32.

Usage:  python3 scripts/cut_inter.py path/to/Inter-VariableFont_opsz,wght.ttf
Inter is SIL Open Font License 1.1; keep OFL.txt beside the font.
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

OUT = 'resources/Inter-SemiBold.ttf'
# Basic Latin, plus the punctuation a UI actually reaches for.
CHARS = list(range(0x20, 0x7F)) + [0xA9, 0x2013, 0x2014,
                                   0x2018, 0x2019, 0x201C, 0x201D, 0x2026]


def main(src):
    font = instancer.instantiateVariableFont(TTFont(src), {'wght': 600,
                                                           'opsz': 14})
    opt = subset.Options()
    opt.layout_features = ['kern', 'liga', 'calt', 'ccmp', 'locl',
                           'mark', 'mkmk', 'tnum']
    opt.name_IDs = ['*']
    opt.name_legacy = True
    opt.notdef_outline = True
    opt.drop_tables = ['DSIG']
    sub = subset.Subsetter(options=opt)
    sub.populate(unicodes=CHARS)
    sub.subset(font)

    # Family "Inter", subfamily "SemiBold" — not a family called "Inter
    # SemiBold". Qt matches on the family and picks the weight, so folding
    # the weight into the family name would make every other weight added
    # later look like an unrelated typeface.
    name = font['name']
    for rec in list(name.names):
        where = (rec.platformID, rec.platEncID, rec.langID)
        if rec.nameID in (1, 16):
            name.setName('Inter', rec.nameID, *where)
        elif rec.nameID in (2, 17):
            name.setName('SemiBold', rec.nameID, *where)
        elif rec.nameID == 4:
            name.setName('Inter SemiBold', 4, *where)
        elif rec.nameID == 6:
            name.setName('Inter-SemiBold', 6, *where)
    font['OS/2'].usWeightClass = 600
    font.save(OUT)
    print(f'{OUT}: {len(CHARS)} codepoints')


if __name__ == '__main__':
    main(sys.argv[1])
