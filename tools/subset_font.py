"""駅名・路線名に出てくる文字だけに絞った太字フォントを作る（docs/ に同梱する 75 KB のもの）。

    python subset_font.py <NotoSansJP[wght].ttf> data/network.txt fonts/NotoSansJP-Bold-subset.ttf

元のフォント: https://github.com/google/fonts/tree/main/ofl/notosansjp （SIL OFL 1.1）
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

src, net, out = sys.argv[1:4]
chars = set()
for l in open(net, encoding="utf-8"):
    t = l.split()
    if t and t[0] in ("line", "station"):
        chars.update(t[-1])
chars.update("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz :/.-()（）・〜駅行方面各停急行快速通勤準遅延分秒両列車上下り内外回")
f = instancer.instantiateVariableFont(TTFont(src), {"wght": 700})
opt = subset.Options(); opt.layout_features = []; opt.notdef_outline = True; opt.name_IDs = ["*"]; opt.name_languages = ["*"]
s = subset.Subsetter(opt); s.populate(text="".join(chars)); s.subset(f)
f.save(out)
print(len(chars), "chars ->", out)
