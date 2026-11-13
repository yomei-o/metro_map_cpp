"""画面に出る文字だけに絞った太字フォントを作る（docs/ に同梱するもの）。

    python tools/subset_font.py <NotoSansJP[wght].ttf> data/network.txt fonts/NotoSansJP-Bold-subset.ttf

入れる文字: 駅名・路線名（network.txt）、行き先（data/odpt_toei.txt・data/odpt_metro.txt）、
画面の文言（metro_map.hpp の文字列をそのまま全部）、ASCII。
元のフォント: https://github.com/google/fonts/tree/main/ofl/notosansjp （SIL OFL 1.1）
"""
import os
import re
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

src, net, out = sys.argv[1:4]
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
chars = set(chr(c) for c in range(0x20, 0x7F))
for l in open(net, encoding="utf-8"):
    t = l.split()
    if t and t[0] in ("line", "station"):
        chars.update(t[-1])
# 行き先（直通先の駅名も）：表の中の ASCII 以外の文字を全部
for name in ("odpt_toei.txt", "odpt_metro.txt"):
    p = os.path.join(root, "data", name)
    if os.path.exists(p):
        chars.update(c for c in open(p, encoding="utf-8").read() if ord(c) > 0x7F)
# 画面の文言：metro_map.hpp の文字列をそのまま全部
for lit in re.findall(r'"([^"]*)"', open(os.path.join(root, "metro_map.hpp"), encoding="utf-8").read()):
    chars.update(lit)
chars.update("0123456789（）・〜：／「」、。駅行方面各停急行快速通勤準遅延分秒両列車上下り内外回")
chars.discard("\n")
f = instancer.instantiateVariableFont(TTFont(src), {"wght": 700})
opt = subset.Options(); opt.layout_features = []; opt.notdef_outline = True; opt.name_IDs = ["*"]; opt.name_languages = ["*"]
opt.hinting = False
s = subset.Subsetter(opt); s.populate(text="".join(chars)); s.subset(f)
f.save(out)
print(len(chars), "chars ->", out)
