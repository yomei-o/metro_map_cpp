// odpt.hpp — 公共交通オープンデータセンター（ODPT）の列車ロケーション（odpt:Train）を地図の駅と区間に結びつける
//
// 使うもの:
//   data/odpt_toei.txt（tools/build_odpt_toei.py が作る）
//       railway <ODPT の路線> <路線記号>
//       station <ODPT の駅> <路線記号> <駅名>
//       run <駅A> <駅B> <秒>            時刻表から出した A → B の平均の所要時間
//       dest <駅> <駅名>                直通先の行き先の名前
//   odpt:Train の JSON（配列）。いまは都営だけがキー無しの公開用 API（api-public.odpt.org）で取れる。
//       owl:sameAs（列車の ID）, odpt:railway, odpt:fromStation, odpt:toStation（駅にいるときは null）,
//       odpt:delay（秒）, odpt:trainNumber, odpt:destinationStation
#pragma once
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"
#include "network.hpp"

namespace mm {

struct OdptTable {
    std::map<std::string, int> railway;                     // ODPT の路線 → 路線番号（net.lines の添字）
    std::map<std::string, int> station;                     // ODPT の駅 → ノード番号
    std::map<std::string, std::string> station_name;        // ODPT の駅 → 駅名
    std::map<std::pair<int, int>, double> run;              // (ノード A, ノード B) → 秒（路線をまたいで同じ 2 駅なら最初のもの）
    std::map<std::pair<int, std::pair<int, int>>, double> run_l;   // (路線, (A, B)) → 秒

    bool empty() const { return railway.empty(); }

    // 失敗した行は数えて返す（駅名が地図に無いなど）。0 なら全部結びついた
    int load(const std::string& text, const Network& net) {
        int bad = 0;
        std::istringstream in(text);
        std::string ln;
        while (std::getline(in, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            std::istringstream s(ln);
            std::string kind; s >> kind;
            if (kind == "railway") {
                std::string id, code; s >> id >> code;
                int l = code.empty() ? -1 : net.line_of(code[0]);
                if (l < 0) { ++bad; continue; }
                railway[id] = l;
            } else if (kind == "station") {
                std::string id, code, name; s >> id >> code; std::getline(s >> std::ws, name);
                int l = code.empty() ? -1 : net.line_of(code[0]), n = -1;
                for (size_t i = 0; i < net.nodes.size() && l >= 0; ++i)
                    if (net.nodes[i].name == name && std::find(net.nodes[i].lines.begin(), net.nodes[i].lines.end(), l) != net.nodes[i].lines.end()) { n = (int)i; break; }
                if (n < 0) { ++bad; continue; }
                station[id] = n; station_name[id] = name;
            } else if (kind == "dest") {
                std::string id, name; s >> id; std::getline(s >> std::ws, name);
                if (!id.empty() && !name.empty()) station_name[id] = name;
            } else if (kind == "run") {
                std::string a, b; double sec = 0; s >> a >> b >> sec;
                auto ia = station.find(a), ib = station.find(b);
                if (ia == station.end() || ib == station.end() || sec <= 0) { ++bad; continue; }
                int l = line_of_station(a);
                run_l[{l, {ia->second, ib->second}}] = sec;
                run.emplace(std::make_pair(ia->second, ib->second), sec);
            }
        }
        return bad;
    }
    // "odpt.Station:Toei.Mita.Hakusan" → その路線の番号（"odpt.Railway:Toei.Mita" から引く）
    int line_of_station(const std::string& st) const {
        auto c = st.find(':'), d = st.rfind('.');
        if (c == std::string::npos || d == std::string::npos || d <= c) return -1;
        auto it = railway.find("odpt.Railway:" + st.substr(c + 1, d - c - 1));
        return it == railway.end() ? -1 : it->second;
    }
    double run_time(int l, int a, int b, double fallback) const {
        auto it = run_l.find({l, {a, b}});
        if (it != run_l.end()) return it->second;
        auto jt = run.find({a, b});
        return jt != run.end() ? jt->second : fallback;
    }
};

// odpt:Train の 1 件を、地図で使う形にしたもの
struct OdptTrain {
    std::string id, number, dest;   // dest は駅名（地図に無い駅なら ODPT の ID の最後の部分）
    int line = -1, from = -1, to = -1;   // to = -1 は from の駅にいる
    double delay = 0;               // 秒
};

// JSON を読んで、地図に載る列車だけを返す（路線か駅が地図に無いもの＝直通先を走っている列車は外す）
inline std::vector<OdptTrain> parse_odpt_trains(const std::string& json_text, const OdptTable& tab, std::string* err = nullptr) {
    std::vector<OdptTrain> out;
    auto j = nlohmann::json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_array()) { if (err) *err = "not a JSON array"; return out; }
    auto str = [](const nlohmann::json& o, const char* k) -> std::string {
        auto it = o.find(k);
        return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    for (auto& o : j) {
        if (!o.is_object()) continue;
        auto rl = tab.railway.find(str(o, "odpt:railway"));
        if (rl == tab.railway.end()) continue;
        auto fr = tab.station.find(str(o, "odpt:fromStation"));
        if (fr == tab.station.end()) continue;
        OdptTrain t;
        t.id = str(o, "owl:sameAs"); t.number = str(o, "odpt:trainNumber");
        t.line = rl->second; t.from = fr->second;
        std::string to = str(o, "odpt:toStation");
        if (!to.empty()) {
            auto ts = tab.station.find(to);
            if (ts == tab.station.end()) continue;   // 直通先へ出ていくところ
            t.to = ts->second;
        }
        if (auto d = o.find("odpt:delay"); d != o.end() && d->is_number()) t.delay = d->get<double>();
        if (auto d = o.find("odpt:destinationStation"); d != o.end() && d->is_array() && !d->empty() && (*d)[0].is_string()) {
            std::string id = (*d)[0].get<std::string>();
            auto nm = tab.station_name.find(id);
            t.dest = nm != tab.station_name.end() ? nm->second : id.substr(id.rfind('.') + 1);
        }
        if (t.id.empty()) continue;
        out.push_back(t);
    }
    return out;
}

}  // namespace mm
