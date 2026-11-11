# 都営地下鉄の ODPT データから、列車をのせるための小さな表 data/odpt_toei.txt を作る。
#
#   python tools/build_odpt_toei.py <dir> > data/odpt_toei.txt
#
# <dir> には ODPT の公開用 API（キー無し、https://api-public.odpt.org/api/v4/）から取った JSON を置く:
#   rw.json            odpt:Railway?odpt:operator=odpt.Operator:Toei
#   tt_Asakusa.json    odpt:TrainTimetable?odpt:railway=odpt.Railway:Toei.Asakusa（Mita, Shinjuku, Oedo も同様）
#
# 出力:
#   railway odpt.Railway:Toei.Mita I                    ODPT の路線 → network.txt の路線記号
#   station odpt.Station:Toei.Mita.Hakusan I 白山       ODPT の駅 → 路線記号と駅名（network.txt の名前と同じ）
#   run <駅A> <駅B> <秒>                                 A を出て B に着くまでの平均（時刻表の隣り合う停車駅から）
#   dest <駅> <駅名>                                     直通先の行き先（他社の駅はキー無しでは名前が取れないので、下の表で）
import json, os, sys
from collections import defaultdict

CODE = {"Asakusa": "A", "Mita": "I", "Shinjuku": "S", "Oedo": "E"}

# 直通先の行き先（時刻表の odpt:destinationStation に出てくる他社の駅）
THROUGH = {
    "ImbaNihonIdai": "印旛日本医大", "InzaiMakinohara": "印西牧の原", "HanedaAirportTerminal1and2": "羽田空港第1・第2ターミナル",
    "KeikyuKurihama": "京急久里浜", "Misakiguchi": "三崎口", "Miurakaigan": "三浦海岸", "KanagawaShimmachi": "神奈川新町",
    "KanazawaBunko": "金沢文庫", "Shinagawa": "品川", "ZushiHayama": "逗子・葉山", "Sasazuka": "笹塚", "Hashimoto": "橋本",
    "KeioTamaCenter": "京王多摩センター", "Wakabadai": "若葉台", "KeiseiNarita": "京成成田", "KeiseiSakura": "京成佐倉",
    "KeiseiTakasago": "京成高砂", "NaritaAirportTerminal1": "成田空港", "Sogosando": "宗吾参道", "Aoto": "青砥",
    "ShibayamaChiyoda": "芝山千代田", "Shonandai": "湘南台", "Ebina": "海老名", "Yamato": "大和", "Nishiya": "西谷",
    "Hiyoshi": "日吉", "MusashiKosugi": "武蔵小杉", "ShinYokohama": "新横浜",
}


def minutes(s):
    h, m = s.split(":")
    return int(h) * 60 + int(m)


def main(d):
    out = ["# 出典: 公共交通オープンデータセンター（ODPT）の東京都交通局のデータを加工して作成"]
    rw = json.load(open(os.path.join(d, "rw.json"), encoding="utf-8"))
    known = set()
    for r in rw:
        key = r["owl:sameAs"].split(".")[-1]
        if key not in CODE:
            continue
        c = CODE[key]
        out.append(f"railway {r['owl:sameAs']} {c}")
        for s in r["odpt:stationOrder"]:
            if s["odpt:station"] in known:
                continue
            known.add(s["odpt:station"])
            out.append(f"station {s['odpt:station']} {c} {s['odpt:stationTitle']['ja']}")
    runs = defaultdict(list)
    dests = set()
    for key in CODE:
        tt = json.load(open(os.path.join(d, f"tt_{key}.json"), encoding="utf-8"))
        for t in tt:
            dests.update(d for d in t.get("odpt:destinationStation") or [] if d not in known)
            objs = t["odpt:trainTimetableObject"]
            for a, b in zip(objs, objs[1:]):
                sa, sb = a.get("odpt:departureStation"), b.get("odpt:arrivalStation") or b.get("odpt:departureStation")
                ta = a.get("odpt:departureTime")
                tb = b.get("odpt:arrivalTime") or b.get("odpt:departureTime")
                if not (sa and sb and ta and tb) or sa not in known or sb not in known:
                    continue
                dt = (minutes(tb) - minutes(ta)) % (24 * 60)
                if 0 <= dt <= 15:
                    runs[(sa, sb)].append(dt)
    for (a, b), v in sorted(runs.items()):
        # 分単位の時刻の差の平均。停車は 0 分のこともあるので、最低 40 秒にする
        out.append(f"run {a} {b} {max(40, round(sum(v) / len(v) * 60))}")
    for dst in sorted(dests):
        name = THROUGH.get(dst.split(".")[-1])
        if name:
            out.append(f"dest {dst} {name}")
        else:
            print(f"warning: 行き先の名前が無い: {dst}", file=sys.stderr)
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    print("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1])
