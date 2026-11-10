// network.hpp — 地下鉄の路線・駅・「駅と駅のあいだ（辺）」を作る（ヘッダーオンリー、依存なし）
//
// 入力は tools/build_network.py が国土数値情報から作った data/network.txt:
//     line <記号> <RRGGBB> <路線名> / track <lon>,<lat> ... / station <lon> <lat> <駅名>
// ここでやること:
//   1. 経緯度をメルカトル図法の平面（km）に投影する
//   2. 同じ名前で近い駅をまとめて「ノード」にする（乗換駅は 1 つの点になる）
//   3. 線路の折れ線に駅を載せ、線路に沿った順に並べて、隣どうしを「辺」にする。
//      辺は、その区間の線路の形（折れ線）を持つ。列車の位置の補間と、地図の描画はこの辺を使う
//   4. 線路を共有する区間（有楽町線と副都心線の和光市〜小竹向原など）は、相手の路線の辺を借りる
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace mm {

struct P2 { double x = 0, y = 0; };
inline P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.y + b.y}; }
inline P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
inline P2 operator*(P2 a, double s) { return {a.x * s, a.y * s}; }
inline double dot(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
inline double len(P2 a) { return std::sqrt(dot(a, a)); }

// メルカトル図法（km）。y は北が負（画面の下向きが正）になるように符号を反転しておく
inline P2 mercator(double lon, double lat) {
    const double R = 6378.137, d2r = 3.14159265358979323846 / 180.0;
    return {R * lon * d2r, -R * std::log(std::tan(3.14159265358979323846 / 4 + lat * d2r / 2))};
}

struct Node {                 // 駅（乗換駅は路線をまたいで 1 つ）
    std::string name;
    P2 geo;                   // 実際の位置（km）
    P2 sch;                   // 路線図風の位置（layout.hpp が決める）
    std::vector<int> lines;   // 通る路線
};

struct Edge {                 // 隣り合う 2 駅のあいだ（路線ごと）
    int line, a, b;           // a → b が路線の並びの向き
    std::vector<P2> geo;      // 線路の形（a から b へ）
    std::vector<P2> sch;      // 路線図風の形（layout.hpp が決める）
    double geo_len = 0;
};

struct Line {
    char code = '?';
    uint32_t color = 0xffffff;
    std::string name;
    std::vector<std::vector<P2>> tracks;   // 元の線路（投影済み）
    std::vector<int> nodes;                // この路線の駅（ノード番号、重複なし）
    std::vector<int> edges;
};

struct Network {
    std::vector<Line> lines;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::string error;
    int line_of(char code) const { for (size_t i = 0; i < lines.size(); ++i) if (lines[i].code == code) return (int)i; return -1; }
    int node_of(const std::string& name) const { for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].name == name) return (int)i; return -1; }
    // 路線 l の a-b の辺（向きは問わない）。無ければ -1
    int edge_of(int l, int a, int b) const {
        if (l < 0) return -1;
        for (int e : lines[l].edges) if ((edges[e].a == a && edges[e].b == b) || (edges[e].a == b && edges[e].b == a)) return e;
        return -1;
    }
};

// 点 p の、折れ線 pl 上でいちばん近い位置（距離・弧長）
inline void project_on(const std::vector<P2>& pl, P2 p, double& dist, double& arc) {
    dist = 1e18; arc = 0;
    double acc = 0;
    for (size_t i = 1; i < pl.size(); ++i) {
        P2 d = pl[i] - pl[i - 1];
        double L = len(d);
        double t = L > 0 ? std::clamp(dot(p - pl[i - 1], d) / (L * L), 0.0, 1.0) : 0;
        double dd = len(pl[i - 1] + d * t - p);
        if (dd < dist) { dist = dd; arc = acc + t * L; }
        acc += L;
    }
}

// 折れ線を弧長 s0..s1 で切り出す（s0 > s1 なら逆向き）
inline std::vector<P2> slice(const std::vector<P2>& pl, double s0, double s1) {
    bool rev = s0 > s1;
    if (rev) std::swap(s0, s1);
    std::vector<P2> out;
    double acc = 0;
    auto at = [&](size_t i, double s) { P2 d = pl[i] - pl[i - 1]; double L = len(d); return pl[i - 1] + d * (L > 0 ? (s - acc) / L : 0); };
    for (size_t i = 1; i < pl.size(); ++i) {
        double L = len(pl[i] - pl[i - 1]);
        if (acc + L >= s0 && acc <= s1) {
            if (out.empty()) out.push_back(at(i, std::max(s0, acc)));
            if (acc + L <= s1) out.push_back(pl[i]);
            else { out.push_back(at(i, s1)); break; }
        }
        acc += L;
    }
    if (out.size() == 1) out.push_back(out[0]);
    if (rev) std::reverse(out.begin(), out.end());
    return out;
}

inline double poly_len(const std::vector<P2>& pl) { double s = 0; for (size_t i = 1; i < pl.size(); ++i) s += len(pl[i] - pl[i - 1]); return s; }

// 線路はつながっているが、列車はその 2 駅のあいだを直接走らない区間（線路の形からは決められないので名前で指定）
//   丸ノ内線の方南町支線は中野坂上の少し西で分かれるので、網の上では新中野からも支線へ入れてしまう
inline std::vector<std::pair<std::string, std::string>> excluded_edges() {
    return {{"新中野", "中野新橋"}};
}

// 他の路線と線路を共有していて、N02 では片方にしか載っていない区間（駅名を順に）
struct Shared { char line; std::vector<std::string> stations; };
inline std::vector<Shared> shared_sections() {
    return {
        {'F', {"和光市", "地下鉄成増", "地下鉄赤塚", "平和台", "氷川台", "小竹向原"}},   // 副都心線は有楽町線の線路
    };
}

inline bool parse_network(const std::string& text, Network& net) {
    struct RawSt { std::string name; P2 p; };
    std::vector<std::vector<RawSt>> raw;
    std::istringstream in(text);
    std::string ln;
    while (std::getline(in, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        if (ln.empty() || ln[0] == '#') continue;
        std::istringstream ls(ln);
        std::string kind; ls >> kind;
        if (kind == "line") {
            Line L; std::string code, col; ls >> code >> col >> L.name;
            L.code = code.empty() ? '?' : code[0];
            L.color = (uint32_t)std::stoul(col, nullptr, 16);
            net.lines.push_back(L); raw.push_back({});
        } else if (kind == "track" && !net.lines.empty()) {
            std::vector<P2> t; std::string tok;
            while (ls >> tok) { size_t c = tok.find(','); t.push_back(mercator(std::stod(tok.substr(0, c)), std::stod(tok.substr(c + 1)))); }
            if (t.size() >= 2) net.lines.back().tracks.push_back(t);
        } else if (kind == "station" && !net.lines.empty()) {
            double lon, lat; std::string name; ls >> lon >> lat >> name;
            raw.back().push_back({name, mercator(lon, lat)});
        }
    }
    if (net.lines.empty()) { net.error = "no line"; return false; }

    // 2. 同じ名前で 0.6 km 以内の駅を 1 つのノードに
    std::vector<std::vector<int>> st_node(raw.size());
    for (size_t l = 0; l < raw.size(); ++l)
        for (auto& s : raw[l]) {
            int found = -1;
            for (size_t n = 0; n < net.nodes.size(); ++n)
                if (net.nodes[n].name == s.name && len(net.nodes[n].geo - s.p) < 0.6) { found = (int)n; break; }
            if (found < 0) { net.nodes.push_back({s.name, s.p, s.p, {}}); found = (int)net.nodes.size() - 1; }
            st_node[l].push_back(found);
            auto& nl = net.nodes[found].lines;
            if (std::find(nl.begin(), nl.end(), (int)l) == nl.end()) nl.push_back((int)l);
            auto& ln2 = net.lines[l].nodes;
            if (std::find(ln2.begin(), ln2.end(), found) == ln2.end()) ln2.push_back(found);
        }
    // 乗換駅の位置は、まとめた駅の平均（geo の初期値は最初の駅なので取り直す）
    {
        std::vector<P2> sum(net.nodes.size()); std::vector<int> cnt(net.nodes.size(), 0);
        for (size_t l = 0; l < raw.size(); ++l) for (size_t i = 0; i < raw[l].size(); ++i) { sum[st_node[l][i]] = sum[st_node[l][i]] + raw[l][i].p; ++cnt[st_node[l][i]]; }
        for (size_t n = 0; n < net.nodes.size(); ++n) if (cnt[n]) net.nodes[n].geo = net.nodes[n].sch = sum[n] * (1.0 / cnt[n]);
    }

    // 3. 路線の線路全体を 1 つの網（20 m おきの点と、そのつながり）にし、各駅をいちばん近い点に結び付ける。
    //    駅から網に沿って広げていき、ほかの駅に最初に着いたら、その 2 駅を辺にする（途中の駅は越えない）。
    //    分岐（北綾瀬・方南町）も輪（大江戸線）も、これで同じに扱える
    for (size_t l = 0; l < net.lines.size(); ++l) {
        Line& L = net.lines[l];
        std::vector<P2> V;
        std::vector<std::vector<std::pair<int, double>>> G;
        std::map<std::pair<long long, long long>, int> key_of;
        auto vid = [&](P2 p) {
            auto k = std::make_pair((long long)std::llround(p.x * 1e5), (long long)std::llround(p.y * 1e5));
            auto it = key_of.find(k);
            if (it != key_of.end()) return it->second;
            V.push_back(p); G.push_back({});
            return key_of[k] = (int)V.size() - 1;
        };
        auto link = [&](int a, int b) { if (a == b) return; double d = len(V[a] - V[b]); G[a].push_back({b, d}); G[b].push_back({a, d}); };
        for (auto& tr : L.tracks) {
            int prev = vid(tr[0]);
            for (size_t i = 1; i < tr.size(); ++i) {
                P2 a = tr[i - 1], b = tr[i];
                int n = std::max(1, (int)std::ceil(len(b - a) / 0.02));
                for (int k = 1; k <= n; ++k) {
                    int cur = k == n ? vid(b) : vid(a + (b - a) * ((double)k / n));
                    link(prev, cur); prev = cur;
                }
            }
        }
        // 駅 → いちばん近い点（0.3 km 以内）
        std::map<int, int> at_vertex;   // 点 → 駅の添字
        std::vector<int> st_v(raw[l].size(), -1);
        for (size_t i = 0; i < raw[l].size(); ++i) {
            int best = -1; double bd = 0.3;
            for (size_t v = 0; v < V.size(); ++v) { double d = len(V[v] - raw[l][i].p); if (d < bd) { bd = d; best = (int)v; } }
            if (best < 0) continue;
            st_v[i] = best;
            if (!at_vertex.count(best)) at_vertex[best] = (int)i;
        }
        for (size_t i = 0; i < raw[l].size(); ++i) {
            if (st_v[i] < 0 || at_vertex[st_v[i]] != (int)i) continue;
            // ダイクストラ（他の駅の点からは先へ広げない）
            std::vector<double> dist(V.size(), 1e18);
            std::vector<int> from(V.size(), -1);
            std::vector<std::pair<double, int>> heap{{0.0, st_v[i]}};
            dist[st_v[i]] = 0;
            while (!heap.empty()) {
                std::pop_heap(heap.begin(), heap.end(), std::greater<>());
                auto [d, v] = heap.back(); heap.pop_back();
                if (d > dist[v]) continue;
                if (v != st_v[i] && at_vertex.count(v)) continue;
                for (auto [w, c] : G[v])
                    if (d + c < dist[w]) { dist[w] = d + c; from[w] = v; heap.push_back({dist[w], w}); std::push_heap(heap.begin(), heap.end(), std::greater<>()); }
            }
            for (auto [v, j] : at_vertex) {
                if (v == st_v[i] || dist[v] >= 1e17) continue;
                int a = st_node[l][i], b = st_node[l][j];
                if (a == b || net.edge_of((int)l, a, b) >= 0) continue;
                Edge e; e.line = (int)l; e.a = a; e.b = b;
                for (int x = v; x >= 0; x = from[x]) e.geo.push_back(V[x]);
                std::reverse(e.geo.begin(), e.geo.end());
                // 両端はノードの位置に合わせる（乗換駅でぴったりつながるように）
                e.geo.front() = net.nodes[a].geo; e.geo.back() = net.nodes[b].geo;
                // 20 m おきの点は多すぎるので、ほぼ一直線の点を間引く
                std::vector<P2> s{e.geo.front()};
                for (size_t k = 1; k + 1 < e.geo.size(); ++k) {
                    P2 d1 = e.geo[k] - s.back(), d2 = e.geo[k + 1] - e.geo[k];
                    double cr = std::fabs(d1.x * d2.y - d1.y * d2.x) / std::max(1e-9, len(d1) * len(d2));
                    if (cr > 0.02 || len(d1) > 0.4) s.push_back(e.geo[k]);
                }
                s.push_back(e.geo.back());
                e.geo = s;
                e.geo_len = poly_len(e.geo);
                // 分岐点が駅のすぐ横にあると（中野坂上・都庁前）、駅の点を通らずに分岐から隣の支線へ抜けて
                // 「駅を飛ばした近道」ができる。途中で同じ路線のほかの駅の 0.3 km 以内を通る辺は捨てる
                bool skips = false;
                for (int nd : L.nodes) {
                    if (nd == a || nd == b) continue;
                    double d, s2; project_on(e.geo, net.nodes[nd].geo, d, s2);
                    if (d < 0.3) { skips = true; break; }
                }
                if (skips) continue;
                for (auto& [x, y] : excluded_edges())
                    if ((net.nodes[a].name == x && net.nodes[b].name == y) || (net.nodes[a].name == y && net.nodes[b].name == x)) skips = true;
                if (skips) continue;
                net.edges.push_back(e);
                L.edges.push_back((int)net.edges.size() - 1);
            }
        }
    }
    // 4. 共有区間：相手の路線の辺を自分の色で借りる
    for (auto& sh : shared_sections()) {
        int l = net.line_of(sh.line);
        if (l < 0) continue;
        for (size_t k = 0; k + 1 < sh.stations.size(); ++k) {
            int a = net.node_of(sh.stations[k]), b = net.node_of(sh.stations[k + 1]);
            if (a < 0 || b < 0 || net.edge_of(l, a, b) >= 0) continue;
            for (size_t o = 0; o < net.lines.size(); ++o) {
                int e0 = net.edge_of((int)o, a, b);
                if (e0 < 0) continue;
                Edge e = net.edges[e0]; e.line = l;
                if (e.a != a) { std::swap(e.a, e.b); std::reverse(e.geo.begin(), e.geo.end()); }
                net.edges.push_back(e);
                net.lines[l].edges.push_back((int)net.edges.size() - 1);
                for (int nd : {a, b}) {
                    auto& nl = net.nodes[nd].lines;
                    if (std::find(nl.begin(), nl.end(), l) == nl.end()) nl.push_back(l);
                    auto& ln2 = net.lines[l].nodes;
                    if (std::find(ln2.begin(), ln2.end(), nd) == ln2.end()) ln2.push_back(nd);
                }
                break;
            }
        }
    }
    for (auto& e : net.edges) e.sch = {net.nodes[e.a].sch, net.nodes[e.b].sch};
    return true;
}

}  // namespace mm
