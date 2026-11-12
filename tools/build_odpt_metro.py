# 東京メトロ 9 路線の時刻表（ダイヤ）を data/odpt_metro.txt に焼く。
#
#   python tools/build_odpt_metro.py <ODPTのアクセストークン> > data/odpt_metro.txt
#
# 東京メトロは列車の位置（odpt:Train）を公開していない（ODPT で位置を出すのは都営と横浜市営だけ）。
# そこでキーの要る時刻表（odpt:TrainTimetable）をビルド時に取り、ブラウザは「いまこの時刻に
# どの列車がどの駅間にいるはず」をダイヤから計算して走らせる（ページにキーは埋めない）。
#
# 出力（odpt_toei.txt と同じ railway/station/order/dest に、trip を足したもの）:
#   railway <ODPTの路線> <路線記号>
#   station <ODPTの駅> <路線記号> <駅名>                 network.txt と同じ駅名
#   order <ODPTの路線> <番号が増える向き> <駅1> <駅2> ...  駅の並び
#   dest <駅> <駅名>                                     直通先の行き先の名前
#   trip <railwayの番号> <0平日/1土休> <1先頭が番号増/0減> <行き先名> <列車番号> <oi>:<秒> <oi>:<秒> ...
#       railwayの番号 = この出力で railway 行が出てくる順（0 から）。丸ノ内線と支線は同じ記号 M だが別の order。
#       oi = その railway の order での駅の番号。秒 = 真夜中からの秒（24 時以降は 86400 超）。
# 標準ライブラリだけ。キーは引数で渡すだけで、出力にもリポにも残らない。
import json, sys, urllib.request
from collections import defaultdict

BASE = "https://api.odpt.org/api/v4/"
CODE = {"Ginza": "G", "Marunouchi": "M", "MarunouchiBranch": "M", "Hibiya": "H", "Tozai": "T",
        "Chiyoda": "C", "Yurakucho": "Y", "Hanzomon": "Z", "Namboku": "N", "Fukutoshin": "F"}
CALS = {"Weekday": 0, "SaturdayHoliday": 1}


def fetch(path, key):
    url = BASE + path + ("&" if "?" in path else "?") + "acl:consumerKey=" + key
    req = urllib.request.Request(url, headers={"User-Agent": "metro-map-build/1.0"})
    with urllib.request.urlopen(req, timeout=90) as r:
        return json.loads(r.read().decode("utf-8"))


def hhmm(s):
    h, m = s.split(":")
    return (int(h) * 60 + int(m)) * 60


def main(key):
    rw = fetch("odpt:Railway?odpt:operator=odpt.Operator:TokyoMetro", key)
    out = ["# 出典: 公共交通オープンデータセンター（ODPT）の東京メトロのデータ（時刻表）を加工して作成"]
    known = set()                 # 駅の並びに出した駅
    known_name = {}               # 駅 → 日本語名（並びにある駅）
    asc_of, order_of, idx_of = {}, {}, {}
    rwi_of = {}                   # railway id → 出力での番号（trip が参照する）
    for r in rw:
        key_name = r["owl:sameAs"].split(".")[-1]
        if key_name not in CODE:
            continue
        c = CODE[key_name]; rid = r["owl:sameAs"]
        rwi_of[rid] = len(rwi_of)
        asc_of[rid] = r.get("odpt:ascendingRailDirection") or ""
        order = [s["odpt:station"] for s in sorted(r["odpt:stationOrder"], key=lambda s: s["odpt:index"])]
        order_of[rid] = order
        idx_of[rid] = {sid: i for i, sid in enumerate(order)}
        out.append(f"railway {rid} {c}")
        out.append(f"order {rid} {asc_of[rid]} " + " ".join(order))
        for s in r["odpt:stationOrder"]:
            sid = s["odpt:station"]
            if sid in known:
                continue
            known.add(sid); known_name[sid] = s["odpt:stationTitle"]["ja"]
            out.append(f"station {sid} {c} {s['odpt:stationTitle']['ja']}")

    dests = set()
    trips = []
    for key_name in CODE:
        rid = f"odpt.Railway:TokyoMetro.{key_name}"
        if rid not in order_of:
            continue
        oi = idx_of[rid]; c = CODE[key_name]
        for cal, calnum in CALS.items():
            tt = fetch(f"odpt:TrainTimetable?odpt:railway={rid}&odpt:calendar=odpt.Calendar:{cal}", key)
            for t in tt:
                asc = 1 if t.get("odpt:railDirection") == asc_of[rid] else 0
                dest_ids = t.get("odpt:destinationStation") or []
                dest = dest_ids[0] if dest_ids else ""
                if dest and dest not in known:
                    dests.add(dest)
                stops = []
                for o in t.get("odpt:trainTimetableObject") or []:
                    sid = o.get("odpt:departureStation") or o.get("odpt:arrivalStation")
                    tm = o.get("odpt:departureTime") or o.get("odpt:arrivalTime")
                    if not sid or not tm or sid not in oi:
                        continue
                    ix = oi[sid]
                    if stops and stops[-1][0] == ix:
                        continue
                    stops.append((ix, hhmm(tm)))
                if len(stops) < 2:
                    continue
                num = t.get("odpt:trainNumber") or t["owl:sameAs"].split(".")[-1]
                seq = " ".join(f"{ix}:{sec}" for ix, sec in stops)
                trips.append([rwi_of[rid], calnum, asc, dest, num, seq])

    # 直通先など、並びに無い行き先の日本語名をキーで引く（ビルド時だけ）
    name_of = {}
    for d in sorted(dests):
        try:
            st = fetch(f"odpt:Station?owl:sameAs={d}", key)
            if st and st[0].get("odpt:stationTitle", {}).get("ja"):
                name_of[d] = st[0]["odpt:stationTitle"]["ja"]
        except Exception as e:
            sys.stderr.write(f"warning: 駅名取得失敗 {d}: {e}\n")
    def destname(d):
        if not d:
            return "-"
        return known_name.get(d) or name_of.get(d) or d.split(".")[-1]

    for rwi, calnum, asc, dest, num, seq in trips:
        out.append(f"trip {rwi} {calnum} {asc} {destname(dest)} {num} {seq}")

    sys.stderr.write(f"railways {len(order_of)} trips {len(trips)} dests {len(dests)}\n")
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    print("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1])
