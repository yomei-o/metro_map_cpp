"""国土数値情報（鉄道データ N02）から、東京の地下鉄 13 路線の線路と駅を取り出し、C++ が読む簡単なテキストにする。

    python build_network.py <N02-24_RailroadSection.geojson> <N02-24_Station.geojson> > data/network.txt

出力（1 行 1 項目、UTF-8）:
    line <記号> <色 RRGGBB> <路線名>
    track <lon>,<lat> <lon>,<lat> ...        線路の折れ線（区間ごと、つながった区間はまとめる）
    station <lon> <lat> <駅名>               ホームの線の中点
路線の順は東京メトロ → 都営。駅は N02 の並び（同じ駅名が 2 回出る場合は最初の 1 つ）。

出典: 国土交通省 国土数値情報（鉄道データ）N02-24、CC BY 4.0 （「国土数値情報（鉄道データ）」（国土交通省）を加工して作成）
"""
import json
import sys
from collections import defaultdict

# N02 の路線名 → (記号, 公式の路線カラー, 表示名)。丸ノ内線の分岐線は丸ノ内線にまとめる
LINES = [
    ("3号線銀座線", "G", "FF9500", "銀座線"),
    ("4号線丸ノ内線", "M", "F62E36", "丸ノ内線"),
    ("4号線丸ノ内線分岐線", "M", "F62E36", "丸ノ内線"),
    ("2号線日比谷線", "H", "B5B5AC", "日比谷線"),
    ("5号線東西線", "T", "009BBF", "東西線"),
    ("9号線千代田線", "C", "00BB85", "千代田線"),
    ("8号線有楽町線", "Y", "C1A470", "有楽町線"),
    ("11号線半蔵門線", "Z", "8F76D6", "半蔵門線"),
    ("7号線南北線", "N", "00AC9B", "南北線"),
    ("13号線副都心線", "F", "9C5E31", "副都心線"),
    ("1号線浅草線", "A", "E85298", "浅草線"),
    ("6号線三田線", "I", "0079C2", "三田線"),
    ("10号線新宿線", "S", "6CBB5A", "新宿線"),
    ("12号線大江戸線", "E", "B6007A", "大江戸線"),
]
OPERATORS = ("東京地下鉄", "東京都")


def key(p):
    return (round(p[0], 6), round(p[1], 6))


def merge_chains(segments):
    """端点を共有する区間を、分かれ目（次数が 2 でない点）まで 1 本の折れ線にまとめる。"""
    adj = defaultdict(list)
    for i, seg in enumerate(segments):
        adj[key(seg[0])].append(i)
        adj[key(seg[-1])].append(i)
    used = [False] * len(segments)
    chains = []

    def walk(start_seg, start_pt):
        pts, seg_i, pt = [], start_seg, start_pt
        while True:
            used[seg_i] = True
            s = segments[seg_i]
            if key(s[0]) != pt:
                s = s[::-1]
            pts.extend(s if not pts else s[1:])
            pt = key(s[-1])
            nxt = [j for j in adj[pt] if not used[j]]
            if len(adj[pt]) != 2 or not nxt:
                return pts
            seg_i = nxt[0]

    # 端点や分かれ目から歩き始め、残り（輪）は任意の点から
    for pt, segs in adj.items():
        if len(segs) != 2:
            for i in segs:
                if not used[i]:
                    chains.append(walk(i, pt))
    for i in range(len(segments)):
        if not used[i]:
            chains.append(walk(i, key(segments[i][0])))
    return chains


def main():
    rail = json.load(open(sys.argv[1], encoding="utf-8"))
    stat = json.load(open(sys.argv[2], encoding="utf-8"))
    out = sys.stdout
    out.reconfigure(encoding="utf-8", newline="\n")
    out.write("# 出典: 「国土数値情報（鉄道データ）」（国土交通省）N02-24 を加工して作成、CC BY 4.0\n")
    done = set()
    for n02_name, code, color, name in LINES:
        segs = [f["geometry"]["coordinates"] for f in rail["features"]
                if f["properties"].get("N02_003") == n02_name and f["properties"].get("N02_004") in OPERATORS]
        sts = [f for f in stat["features"]
               if f["properties"].get("N02_003") == n02_name and f["properties"].get("N02_004") in OPERATORS]
        if code not in done:
            out.write(f"line {code} {color} {name}\n")
            done.add(code)
        for ch in merge_chains(segs):
            out.write("track " + " ".join(f"{x:.6f},{y:.6f}" for x, y in ch) + "\n")
        seen = set()
        for f in sts:
            nm = f["properties"]["N02_005"]
            if nm in seen:
                continue
            seen.add(nm)
            c = f["geometry"]["coordinates"]
            mx = sum(p[0] for p in c) / len(c); my = sum(p[1] for p in c) / len(c)
            out.write(f"station {mx:.6f} {my:.6f} {nm}\n")
        print(f"{name}: {len(segs)} sections -> tracks, {len(seen)} stations", file=sys.stderr)


if __name__ == "__main__":
    main()
