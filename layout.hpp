// layout.hpp — 路線図風の配置を計算する（ヘッダーオンリー、依存なし、乱数なし＝毎回同じ結果）
//
// 実際の駅の位置から出発し、次の力を少しずつ同時にかけて駅を動かす:
//   - 向き   : 隣の駅への向きを 0°/45°/90°/… のいちばん近い向きに寄せる
//   - 間隔   : 隣の駅との距離を、そろった長さ（spacing）に寄せる。都心の詰まった区間は広がり、郊外の長い区間は縮む
//   - まっすぐ: 同じ路線で前後に駅がある途中駅は、前後の中点へ寄せる（無駄な折れ曲がりを減らす）
//   - 離す   : 隣どうしでない駅が近すぎたら押し離す
//   - 錨     : 実際の位置（を全体の大きさに合わせたもの）へ弱く引く。東西南北の並びが崩れないように
// 最後に、各辺を「斜め 45° と水平・垂直の 2 本の線分」に分けて描く形（sch）を作る。
#pragma once
#include <cmath>
#include <array>
#include <map>
#include <set>
#include <vector>

#include "network.hpp"

namespace mm {

struct LayoutCfg {
    int iters = 4000;
    double spacing = 1.0;     // 駅の間隔（路線図の単位）
    double k_dir = 0.35, k_len = 0.25, k_straight = 0.12, k_repel = 0.5, k_anchor = 0.004;
    double min_dist = 0.75;   // これより近い無関係な駅は離す（spacing に対して）
    double gamma = 0.6;       // 魚眼の強さ（1 = なし、小さいほど都心を広げる）
};

inline double snap45(double ang) { const double q = 3.14159265358979323846 / 4; return std::round(ang / q) * q; }

inline void compute_schematic(Network& net, const LayoutCfg& c = {}) {
    const size_t N = net.nodes.size();
    if (!N) return;
    // 隣接（路線をまたいで重複なし）
    std::set<std::pair<int, int>> adj;
    for (auto& e : net.edges) adj.insert({std::min(e.a, e.b), std::max(e.a, e.b)});
    std::vector<std::pair<int, int>> E(adj.begin(), adj.end());
    // まっすぐにしたい 3 駅の組（同じ路線で前後がそれぞれ 1 本ずつの途中駅）
    std::vector<std::array<int, 3>> mids;
    for (size_t l = 0; l < net.lines.size(); ++l) {
        std::map<int, std::vector<int>> nb;
        for (int ei : net.lines[l].edges) { nb[net.edges[ei].a].push_back(net.edges[ei].b); nb[net.edges[ei].b].push_back(net.edges[ei].a); }
        for (auto& [n, v] : nb) if (v.size() == 2) mids.push_back({v[0], n, v[1]});
    }
    // 錨：実際の位置を、辺の長さの中央値が spacing になるように縮めたもの
    std::vector<double> el;
    for (auto& [a, b] : E) el.push_back(len(net.nodes[a].geo - net.nodes[b].geo));
    std::nth_element(el.begin(), el.begin() + el.size() / 2, el.end());
    const double s0 = c.spacing / std::max(1e-6, el[el.size() / 2]);
    // 都心は駅が詰まっているので、中心からの距離を r^gamma で縮める「魚眼」をかけてから錨にする
    // （都心が広がり、郊外が縮む。路線図がたいていやっていること）
    P2 ctr{0, 0};
    for (auto& n : net.nodes) ctr = ctr + n.geo;
    ctr = ctr * (1.0 / N);
    double rmax = 1e-9;
    for (auto& n : net.nodes) rmax = std::max(rmax, len(n.geo - ctr));
    std::vector<P2> fish(N);
    for (size_t i = 0; i < N; ++i) {
        P2 d = net.nodes[i].geo - ctr;
        double r = len(d);
        fish[i] = r > 0 ? d * (rmax * std::pow(r / rmax, c.gamma) / r) : d;
    }
    {
        std::vector<double> fl;
        for (auto& [a, b] : E) fl.push_back(len(fish[a] - fish[b]));
        std::nth_element(fl.begin(), fl.begin() + fl.size() / 2, fl.end());
        double sf = c.spacing / std::max(1e-9, fl[fl.size() / 2]);
        for (auto& q : fish) q = q * sf;
    }
    (void)s0;
    std::vector<P2> anchor(N), p(N);
    for (size_t i = 0; i < N; ++i) p[i] = anchor[i] = fish[i];

    std::vector<P2> f(N);
    for (int it = 0; it < c.iters; ++it) {
        const double cool = 1.0 - 0.7 * it / c.iters;   // だんだん小さく動かす
        std::fill(f.begin(), f.end(), P2{0, 0});
        for (auto& [a, b] : E) {
            P2 d = p[b] - p[a];
            double L = len(d);
            if (L < 1e-9) { d = {1e-3, 0}; L = 1e-3; }
            double ang = snap45(std::atan2(d.y, d.x));
            P2 want = P2{std::cos(ang), std::sin(ang)} * (L + (c.spacing - L) * c.k_len / std::max(c.k_dir, 1e-9));
            P2 corr = (want - d) * (c.k_dir * 0.5);
            f[a] = f[a] - corr; f[b] = f[b] + corr;
        }
        for (auto& m : mids) {
            P2 mid = (p[m[0]] + p[m[2]]) * 0.5;
            f[m[1]] = f[m[1]] + (mid - p[m[1]]) * c.k_straight;
        }
        const double md = c.min_dist * c.spacing;
        for (size_t i = 0; i < N; ++i)
            for (size_t j = i + 1; j < N; ++j) {
                P2 d = p[j] - p[i];
                double L = len(d);
                if (L >= md || adj.count({(int)i, (int)j})) continue;
                if (L < 1e-9) { d = {1e-3 * (double)(i % 7 + 1), 1e-3 * (double)(j % 5 + 1)}; L = len(d); }
                P2 push = d * ((md - L) / L * c.k_repel * 0.5);
                f[i] = f[i] - push; f[j] = f[j] + push;
            }
        for (size_t i = 0; i < N; ++i) {
            f[i] = f[i] + (anchor[i] - p[i]) * c.k_anchor;
            p[i] = p[i] + f[i] * cool;
        }
    }
    for (size_t i = 0; i < N; ++i) net.nodes[i].sch = p[i];

    // 辺の形：向きがすでに 45° の倍数ならそのまま、そうでなければ 2 本の線分（斜め＋水平/垂直）に
    for (auto& e : net.edges) {
        P2 a = net.nodes[e.a].sch, b = net.nodes[e.b].sch, d = b - a;
        double ad = std::fabs(std::fabs(d.x) - std::fabs(d.y));
        bool ok = std::fabs(d.x) < 1e-3 || std::fabs(d.y) < 1e-3 || ad < 1e-3;
        if (ok) { e.sch = {a, b}; continue; }
        double m = std::min(std::fabs(d.x), std::fabs(d.y));
        P2 diag{(d.x > 0 ? 1 : -1) * m, (d.y > 0 ? 1 : -1) * m};
        // 長い方の軸を先に走り、最後に斜めで駅に入る
        e.sch = {a, b - diag, b};
    }
}

}  // namespace mm
