// metro_map.hpp — 地下鉄の地図を描く（実際の地図風 ⇄ 路線図風、光る線、駅名、走る列車）
//
// 使い方（ネイティブも WASM も同じ）:
//   mm::Map m; m.load(network_text, font_bytes, n);   // 路線図風の配置もここで計算する
//   m.resize(1280, 800); m.fit();
//   m.set_mode(1);            // 0 = 実際の地図風、1 = 路線図風（なめらかに変形する）
//   m.tick(dt_seconds);       // 変形と列車を進める
//   const uint8_t* rgba = m.render();
//   m.set_here(lat, lon);     // 現在地（スマホの GPS など）。東京都（本土）の中のときだけ赤く光る玉で出す
//   m.hover(x, y); m.hit(x, y); m.click(x, y);   // 操作。パネル・ボタン・凡例・吹き出し・説明もすべてここで描く
//
// 地図（線・駅・駅名・光）とパネルは表示が変わったときだけ描き直してとっておき、毎フレームは列車だけを重ねる。
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "layout.hpp"
#include "network.hpp"
#include "odpt.hpp"
#include "raster.hpp"

namespace mm {

struct Train {
    int line = 0, edge = 0;
    double t = 0;        // 辺の上の位置 0..1（edge.a → edge.b の向きで測る）
    int dir = 1;         // +1 = a → b、-1 = b → a
    double dwell = 0;    // 駅での停車の残り（秒）
    bool live = false;   // ODPT から来た列車か（仮の列車なら false）
    bool sched = false;  // メトロのダイヤ（時刻表）で動かす列車か
    std::string label;   // 行き先など（表示用）
    int prev_edge = -1, prev_dir = 1;   // 1 つ前の区間（尾をそこまで伸ばすため）
    // ODPT の列車だけが使う
    std::string id, number, dest;
    double delay = 0;
    int from = -1, to = -1;      // to = -1 は from の駅にいる。それ以外は from → to を走っている
    int asc = -1;                // 走る向き（OdptTrain::asc）。止まっているときの尾の向きに使う
    double p0 = 0, t0 = 0, rate = 0;   // 走っている割合 p = p0 + (wall - t0) * rate
    bool confirmed = false;      // to に着いたことがわかっている（なら最後まで進めてよい。わからなければ手前で待つ）
};

struct Map {
    Network net;
    Font font;
    int W = 0, H = 0;
    double mode = 0, target = 0;          // 0 = 実際の地図風、1 = 路線図風
    P2 center{0, 0};                      // 画面の中心（世界座標＝km）
    double scale = 1;                     // 1 km あたりの px
    double ui = 1;                        // 描画 px と画面の px の比（スマホは 1.5 など）。文字と線の最小の太さに掛ける
    std::vector<Train> trains;
    double sim_speed = 1;                 // 仮の列車の速さの倍率（1 = 実時間。ODPT の列車と並べるので実時間のまま）
    double clock = 0;
    double wall = 0;                      // いまの時刻（UNIX 秒）。ODPT の列車はこれで動かす
    bool simulate = true;
    OdptTable odpt;
    std::vector<char> live_line;          // ODPT の列車に置き換えた路線（仮の列車は出さない）
    int live_count = 0;                   // いま地図に載っている ODPT の列車の数
    Canvas base, glow, frame;
    std::vector<uint8_t> rgba;
    bool dirty = true;
    double last_mode = -1;
    // 描画の調子
    Rgb bg_top = {0.035f, 0.045f, 0.075f}, bg_bot = {0.015f, 0.02f, 0.035f};

    // 路線図風の位置は、実際の地図と同じくらいの大きさに合わせた km 座標で持つ（変形の途中も同じ View で描ける）
    std::vector<P2> sch_km;
    std::vector<std::vector<P2>> edge_sch_km;
    // 東京都の外周（pref）と 23 区の区界（ward）。実際の地図のときだけ薄く描く。km 座標（mercator）
    std::vector<std::vector<P2>> admin_pref, admin_ward;
    // 東京メトロのダイヤ（時刻表）。メトロは列車位置を公開していないので、時刻表どおりの位置で走らせる
    struct MRail { int line = -1; std::vector<int> nodes; };                 // network のノード番号を駅の並び順で
    struct MTrip { int rail = 0, cal = 0, asc = -1; std::string dest, num; std::vector<std::pair<int,int>> stops; };  // stops: (order 番号, 真夜中からの秒)
    std::vector<MRail> mrails;
    std::vector<MTrip> mtrips;
    std::vector<char> sched_line;     // ダイヤで動かす路線（そこは仮の列車を出さない）
    int metro_cal = 0;                // 0=平日 1=土休（ページが渡す）
    int metro_count = 0;
    double last_metro = -1;
    // 現在地（ページが navigator.geolocation から受け取って渡す）。都内（本土）のときだけ赤く光る玉で出す
    bool here_on = false;
    P2 here;                          // km（mercator）
    // 画面の部品（パネル・ボタン・凡例・吹き出し・説明）
    struct Box { float x0, y0, x1, y1; int id; };
    enum { BTN_GEO = 1, BTN_SCH = 2, BTN_FIT = 3, PANEL = 100 };
    std::vector<Box> buttons, panels;     // panels はパネルの四角（列車の上に戻す・ドラッグを始めない）
    Canvas info;                          // 地図（base）にパネルを重ねたもの
    bool dirty_ui = true;
    int odpt_state = 0;                   // 都営の列車：0 = まだ、1 = 取れた、2 = 取れなかった
    double odpt_time = 0;                 // 最後に取れた時刻
    double hover_x = -1, hover_y = -1;
    bool auto_cal = true;                 // 平日／土休を wall（日本時間）から自分で決める

    bool load(const std::string& text, const unsigned char* font_bytes, size_t font_n) {
        if (!parse_network(text, net)) return false;
        if (font_bytes && font_n) font.load(font_bytes, font_n);
        compute_schematic(net);
        // 路線図の単位 → km：外接矩形が実際の地図と同じ大きさになるように
        auto bbox = [](const std::vector<P2>& v, P2& lo, P2& hi) {
            lo = {1e18, 1e18}; hi = {-1e18, -1e18};
            for (auto& p : v) { lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); }
        };
        std::vector<P2> g, s;
        for (auto& n : net.nodes) { g.push_back(n.geo); s.push_back(n.sch); }
        P2 glo, ghi, slo, shi; bbox(g, glo, ghi); bbox(s, slo, shi);
        double k = std::min((ghi.x - glo.x) / std::max(1e-9, shi.x - slo.x), (ghi.y - glo.y) / std::max(1e-9, shi.y - slo.y)) * 0.85;
        P2 gc = (glo + ghi) * 0.5, sc = (slo + shi) * 0.5;
        auto to_km = [&](P2 p) { return gc + (p - sc) * k; };
        sch_km.resize(net.nodes.size());
        for (size_t i = 0; i < net.nodes.size(); ++i) sch_km[i] = to_km(net.nodes[i].sch);
        edge_sch_km.resize(net.edges.size());
        for (size_t i = 0; i < net.edges.size(); ++i) { edge_sch_km[i].clear(); for (auto& p : net.edges[i].sch) edge_sch_km[i].push_back(to_km(p)); }
        seed_trains();
        return true;
    }

    void resize(int w, int h) {
        W = w; H = h;
        base.resize(w, h); glow.resize(w, h); frame.resize(w, h); info.resize(w, h);
        rgba.assign((size_t)w * h * 4, 255);
        dirty = true;
    }
    void fit() {
        P2 lo{1e18, 1e18}, hi{-1e18, -1e18};
        for (auto& n : net.nodes) for (P2 p : {n.geo, sch_km[&n - &net.nodes[0]]}) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
        center = (lo + hi) * 0.5;
        scale = std::min(W / std::max(1e-9, hi.x - lo.x), H / std::max(1e-9, hi.y - lo.y)) * 0.92;
        dirty = true;
    }
    void pan(double dx, double dy) { center.x -= dx / scale; center.y -= dy / scale; dirty = true; }
    void zoom(double f, double sx, double sy) {
        P2 w = to_world(sx, sy);
        scale = std::clamp(scale * f, 5.0, 2000.0);
        P2 w2 = to_world(sx, sy);
        center = center + (w - w2);
        dirty = true;
    }
    void set_mode(int m) { target = m ? 1 : 0; }

    P2 to_world(double sx, double sy) const { return {center.x + (sx - W / 2.0) / scale, center.y + (sy - H / 2.0) / scale}; }
    V2 to_screen(P2 p) const { return {(float)((p.x - center.x) * scale + W / 2.0), (float)((p.y - center.y) * scale + H / 2.0)}; }

    // 変形の途中の位置（なめらかに：両端でゆっくり）
    double ease() const { double t = mode; return t * t * (3 - 2 * t); }
    P2 node_pos(int n) const { double e = ease(); return net.nodes[n].geo * (1 - e) + sch_km[n] * e; }

    // 辺の形を m 点に取り直す（弧長で等分）
    static std::vector<P2> resample(const std::vector<P2>& pl, int m) {
        std::vector<P2> out;
        double L = poly_len(pl);
        if (L <= 0) { out.assign(m, pl.empty() ? P2{} : pl[0]); return out; }
        size_t i = 1; double acc = 0;
        for (int k = 0; k < m; ++k) {
            double s = L * k / (m - 1);
            while (i + 1 < pl.size() && acc + len(pl[i] - pl[i - 1]) < s) { acc += len(pl[i] - pl[i - 1]); ++i; }
            double sl = len(pl[i] - pl[i - 1]);
            out.push_back(pl[i - 1] + (pl[i] - pl[i - 1]) * (sl > 0 ? std::clamp((s - acc) / sl, 0.0, 1.0) : 0.0));
        }
        return out;
    }
    std::vector<P2> edge_shape(int e) const {
        double ez = ease();
        if (ez <= 0) return net.edges[e].geo;
        if (ez >= 1) return edge_sch_km[e];
        const int M = 24;
        auto a = resample(net.edges[e].geo, M), b = resample(edge_sch_km[e], M);
        for (int k = 0; k < M; ++k) a[k] = a[k] * (1 - ez) + b[k] * ez;
        return a;
    }

    // 同じ 2 駅を結ぶ辺が複数の路線にあるとき、横に並べるための番号（中心からのずれ）
    double edge_offset(int e) const {
        const Edge& E = net.edges[e];
        int idx = 0, cnt = 0;
        for (size_t o = 0; o < net.edges.size(); ++o) {
            const Edge& F = net.edges[o];
            if ((F.a == E.a && F.b == E.b) || (F.a == E.b && F.b == E.a)) { if ((int)o == e) idx = cnt; ++cnt; }
        }
        return idx - (cnt - 1) / 2.0;
    }
    // 画面上で折れ線を左右にずらす（向きは a → b を正にそろえる）
    static std::vector<V2> offset(const std::vector<V2>& p, float d, bool flip) {
        if (d == 0 || p.size() < 2) return p;
        if (flip) d = -d;
        std::vector<V2> out(p.size());
        for (size_t i = 0; i < p.size(); ++i) {
            V2 a = p[i ? i - 1 : 0], b = p[i + 1 < p.size() ? i + 1 : i];
            float dx = b.x - a.x, dy = b.y - a.y, L = std::sqrt(dx * dx + dy * dy);
            if (L < 1e-6f) { out[i] = p[i]; continue; }
            out[i] = {p[i].x - dy / L * d, p[i].y + dx / L * d};
        }
        return out;
    }

    float line_width() const { return (float)std::clamp(scale * 0.11, 2.0 * ui, 9.0 * ui); }

    // ---- 地図（線・駅・駅名）を base に描く ----
    void draw_base() {
        for (int y = 0; y < H; ++y) {
            Rgb c = lin(mix(bg_top, bg_bot, (float)y / std::max(1, H - 1)));
            for (int x = 0; x < W; ++x) { float* p = &base.px[((size_t)y * W + x) * 3]; p[0] = c.r; p[1] = c.g; p[2] = c.b; }
        }
        // うっすらと格子（実際の地図のときだけ、1 km 間隔）
        double ez = ease();
        if (ez < 1 && scale > 25) {
            P2 lo = to_world(0, 0), hi = to_world(W, H);
            for (double gx = std::floor(lo.x); gx <= hi.x; gx += 1) { V2 a = to_screen({gx, lo.y}), b = to_screen({gx, hi.y}); polyline(base, {a, b}, 1, {0.2f, 0.25f, 0.35f}, 0.12f * (float)(1 - ez)); }
            for (double gy = std::floor(lo.y); gy <= hi.y; gy += 1) { V2 a = to_screen({lo.x, gy}), b = to_screen({hi.x, gy}); polyline(base, {a, b}, 1, {0.2f, 0.25f, 0.35f}, 0.12f * (float)(1 - ez)); }
        }
        // 東京都の外周と 23 区の区界（実際の地図のときだけ、うっすら。路線図へ変形するほど消える）
        if (ez < 1 && (!admin_pref.empty() || !admin_ward.empty())) {
            float a = (float)(1 - ez);
            auto draw_rings = [&](const std::vector<std::vector<P2>>& rs, float w, Rgb col, float al) {
                for (auto& r : rs) { std::vector<V2> s; s.reserve(r.size()); for (auto& p : r) s.push_back(to_screen(p)); polyline(base, s, w, col, al * a); }
            };
            draw_rings(admin_ward, std::max(1.f, (float)ui), {0.26f, 0.31f, 0.44f}, 0.22f);
            draw_rings(admin_pref, std::max(1.f, 1.3f * (float)ui), {0.36f, 0.42f, 0.56f}, 0.33f);
        }
        std::fill(glow.px.begin(), glow.px.end(), 0.f);
        const float lw = line_width();
        std::vector<std::vector<V2>> shapes(net.edges.size());
        for (size_t e = 0; e < net.edges.size(); ++e) {
            std::vector<V2> s;
            for (auto& p : edge_shape((int)e)) s.push_back(to_screen(p));
            const Edge& E = net.edges[e];
            shapes[e] = offset(s, (float)edge_offset((int)e) * lw * 1.1f, E.a > E.b);
        }
        // 光（線を太めに足し、あとでぼかす）
        for (size_t e = 0; e < net.edges.size(); ++e) polyline(glow, shapes[e], lw * 1.6f, hex(net.lines[net.edges[e].line].color), 0.9f, true);
        bloom(base, glow, std::max(2, (int)(lw * 0.8f)), 0.85f);
        // 線：暗い縁取り → 路線カラー
        for (size_t e = 0; e < net.edges.size(); ++e) polyline(base, shapes[e], lw + 2.5f, {0.02f, 0.025f, 0.04f}, 0.9f);
        for (size_t e = 0; e < net.edges.size(); ++e) polyline(base, shapes[e], lw, hex(net.lines[net.edges[e].line].color), 1.f);
        // 駅
        const float r = std::max(1.5f, lw * 0.55f);
        for (size_t n = 0; n < net.nodes.size(); ++n) {
            V2 p = to_screen(node_pos((int)n));
            if (p.x < -20 || p.y < -20 || p.x > W + 20 || p.y > H + 20) continue;
            bool xfer = net.nodes[n].lines.size() > 1;
            if (xfer) { disc(base, p, r * 1.9f, {0.02f, 0.025f, 0.04f}); disc(base, p, r * 1.55f, {1, 1, 1}); disc(base, p, r * 0.8f, {0.08f, 0.09f, 0.12f}); }
            else { disc(base, p, r + 1.2f, {0.02f, 0.025f, 0.04f}); disc(base, p, r, {0.95f, 0.96f, 1.f}); }
        }
        draw_labels(r);
    }

    // 駅名：乗換駅を先に、重ならない場所（右・左・上・下）に置く。小さく表示しているときは乗換駅だけ
    void draw_labels(float r) {
        if (!font.ok) return;
        const int px = (int)std::clamp(scale * 0.32, 10.0 * ui, 16.0 * ui);
        const bool all = scale > 60 * ui;
        struct Box { float x0, y0, x1, y1; };
        std::vector<Box> used;
        for (size_t n = 0; n < net.nodes.size(); ++n) { V2 p = to_screen(node_pos((int)n)); used.push_back({p.x - r, p.y - r, p.x + r, p.y + r}); }
        std::vector<int> order(net.nodes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return net.nodes[a].lines.size() > net.nodes[b].lines.size(); });
        for (int n : order) {
            bool xfer = net.nodes[n].lines.size() > 1;
            if (!all && !xfer) continue;
            V2 p = to_screen(node_pos(n));
            if (p.x < 0 || p.y < 0 || p.x > W || p.y > H) continue;
            const std::string& s = net.nodes[n].name;
            int fpx = xfer ? px + 1 : px;
            float tw = font.width(s, fpx), th = fpx * 0.9f, g = r * 1.9f + 3;
            V2 cand[4] = {{p.x + g, p.y + th * 0.38f}, {p.x - g - tw, p.y + th * 0.38f}, {p.x - tw / 2, p.y - g}, {p.x - tw / 2, p.y + g + th}};
            for (auto c : cand) {
                Box b{c.x - 2, c.y - th - 1, c.x + tw + 2, c.y + 3};
                if (b.x0 < 0 || b.y0 < 0 || b.x1 > W || b.y1 > H) continue;
                bool hit = false;
                for (auto& u : used) if (!(b.x1 < u.x0 || b.x0 > u.x1 || b.y1 < u.y0 || b.y0 > u.y1)) { hit = true; break; }
                if (hit) continue;
                font.draw(base, c.x, c.y, s, fpx, xfer ? Rgb{1, 1, 1} : Rgb{0.82f, 0.85f, 0.9f}, {0.01f, 0.012f, 0.02f}, 2.f);
                used.push_back(b);
                break;
            }
        }
    }

    // ---- 列車 ----
    // 仮の列車：路線の辺に、だいたい 3 区間に 1 本、向きを交互に置く
    void seed_trains() {
        trains.clear();
        for (size_t l = 0; l < net.lines.size(); ++l) {
            const auto& es = net.lines[l].edges;
            for (size_t k = 0; k < es.size(); k += 3) {
                Train t; t.line = (int)l; t.edge = es[k];
                t.t = std::fmod(0.37 * (double)(k + 1) + 0.13 * l, 1.0);
                t.dir = (k / 3) % 2 ? -1 : 1;
                trains.push_back(t);
            }
        }
    }
    // 次の辺：いまの駅で、同じ路線の別の辺へ（行き止まりなら折り返す）
    void advance(Train& t) {
        const Edge& E = net.edges[t.edge];
        int at = t.dir > 0 ? E.b : E.a;
        int next = -1;
        for (int e : net.lines[t.line].edges) {
            if (e == t.edge) continue;
            const Edge& F = net.edges[e];
            if (F.a == at || F.b == at) { next = e; break; }
        }
        if (next < 0) { t.dir = -t.dir; t.dwell = 40; t.prev_edge = -1; return; }
        t.prev_edge = t.edge; t.prev_dir = t.dir;
        t.edge = next;
        t.dir = net.edges[next].a == at ? 1 : -1;
        t.t = t.dir > 0 ? 0 : 1;
        t.dwell = 25;
    }
    void tick(double dt) {
        clock += dt;
        if (auto_cal) {   // きょうのダイヤ（平日／土休）。日付が変わったら置き直す
            int cal = holiday_cal(wall);
            if (cal != metro_cal) { metro_cal = cal; last_metro = -1; }
        }
        // メトロのダイヤ：約 1.5 秒ごとに「いまの時刻の列車」を置き直す（あいだは下の補間でなめらかに動く）
        if (!mtrips.empty() && (last_metro < 0 || wall - last_metro >= 1.5 || wall < last_metro)) { last_metro = wall; apply_metro(); }
        if (mode != target) {
            double step = dt / 1.2;
            mode = mode < target ? std::min(target, mode + step) : std::max(target, mode - step);
            dirty = true;
        }
        for (auto& t : trains) {
            if (!t.live || t.to < 0) continue;
            double p = live_p(t);
            if (t.confirmed && p >= 1) { place_at(t, t.to); continue; }
            t.t = t.dir > 0 ? p : 1 - p;
        }
        if (!simulate) return;
        double sdt = dt * sim_speed;
        for (auto& t : trains) {
            if (t.live) continue;
            if (t.dwell > 0) { t.dwell -= sdt; continue; }
            double L = std::max(0.05, net.edges[t.edge].geo_len);
            double v = 0.011;   // km/s（約 40 km/h、駅間の平均）
            t.t += t.dir * v * sdt / L;
            if (t.t > 1 || t.t < 0) { t.t = std::clamp(t.t, 0.0, 1.0); advance(t); }
        }
    }
    // ---- ODPT の列車 ----
    // ODPT が教えてくれるのは「どの駅にいるか」「どの駅とどの駅のあいだにいるか」だけで、更新は約 30 秒ごと。
    // 駅のあいだの位置は、時刻表から出した区間の所要時間（data/odpt_toei.txt の run）で進める。
    //   - あいだにいると言われた: 所要時間で進める。着いたと言われるまでは 97% の手前で待つ
    //   - 着いたと言われた: 残りを数秒で進めて駅に止める
    //   - 前の駅から次の駅へ、あいだを見ないまま移っていた: 遅れを取り戻すよう早めに走らせる
    int load_odpt(const std::string& table_text) { return odpt.load(table_text, net); }

    // 東京都の外周と 23 区の区界を読む（data/admin_tokyo.txt、lon,lat）。km（mercator）に直して持つ
    int load_admin(const std::string& text) {
        admin_pref.clear(); admin_ward.clear();
        std::istringstream in(text); std::string ln; int which = -1;   // 0=pref 1=ward
        while (std::getline(in, ln)) {
            if (ln.empty() || ln[0] == '#') continue;
            if (ln.rfind("pref", 0) == 0) { which = 0; continue; }
            if (ln.rfind("ward", 0) == 0) { which = 1; continue; }
            if (ln.rfind("ring ", 0) == 0 && which >= 0) {
                std::vector<P2> r; std::istringstream ls(ln.substr(5)); std::string tok;
                while (ls >> tok) { size_t c = tok.find(','); if (c == std::string::npos) continue; r.push_back(mercator(std::stod(tok.substr(0, c)), std::stod(tok.substr(c + 1)))); }
                if (r.size() >= 2) (which == 0 ? admin_pref : admin_ward).push_back(std::move(r));
            }
        }
        dirty = true;
        return (int)(admin_pref.size() + admin_ward.size());
    }

    // 東京メトロのダイヤ（data/odpt_metro.txt）を読む。戻り値は読み込んだ列車（trip）の数
    int load_metro(const std::string& text) {
        mrails.clear(); mtrips.clear();
        std::map<std::string, int> sid_node;    // ODPT の駅 → network のノード
        std::map<std::string, int> rail_of;     // ODPT の路線 → mrails の番号
        std::vector<std::string> order_lines, trip_lines;
        std::istringstream in(text); std::string ln;
        while (std::getline(in, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (ln.empty() || ln[0] == '#') continue;
            std::istringstream s(ln); std::string kind; s >> kind;
            if (kind == "railway") {
                std::string id, code; s >> id >> code;
                int l = code.empty() ? -1 : net.line_of(code[0]);
                rail_of[id] = (int)mrails.size();
                MRail r; r.line = l; mrails.push_back(r);
            } else if (kind == "station") {
                std::string id, code, name; s >> id >> code; std::getline(s >> std::ws, name);
                int l = code.empty() ? -1 : net.line_of(code[0]), n = -1;
                for (size_t i = 0; i < net.nodes.size() && l >= 0; ++i)
                    if (net.nodes[i].name == name && std::find(net.nodes[i].lines.begin(), net.nodes[i].lines.end(), l) != net.nodes[i].lines.end()) { n = (int)i; break; }
                sid_node[id] = n;
            } else if (kind == "order") {
                order_lines.push_back(ln);
            } else if (kind == "trip") {
                trip_lines.push_back(ln);
            }
        }
        for (auto& ol : order_lines) {
            std::istringstream s(ol); std::string kind, rid, asc, st; s >> kind >> rid >> asc;
            auto it = rail_of.find(rid);
            if (it == rail_of.end()) continue;
            auto& nodes = mrails[it->second].nodes; nodes.clear();
            while (s >> st) { auto f = sid_node.find(st); nodes.push_back(f == sid_node.end() ? -1 : f->second); }
        }
        sched_line.assign(net.lines.size(), 0);
        for (auto& tl : trip_lines) {
            std::istringstream s(tl); std::string kind, tok; s >> kind;
            MTrip t; s >> t.rail >> t.cal >> t.asc >> t.dest >> t.num;
            while (s >> tok) { size_t c = tok.find(':'); if (c == std::string::npos) continue; t.stops.push_back({std::stoi(tok.substr(0, c)), std::stoi(tok.substr(c + 1))}); }
            if (t.rail < 0 || t.rail >= (int)mrails.size() || t.stops.size() < 2) continue;
            if (mrails[t.rail].line >= 0 && mrails[t.rail].line < (int)sched_line.size()) sched_line[mrails[t.rail].line] = 1;
            mtrips.push_back(std::move(t));
        }
        last_metro = -1;   // 次の tick で反映
        return (int)mtrips.size();
    }

    // ダイヤから「いまこの時刻の列車の位置」を作って、メトロの列車を置き直す（約 1.5 秒ごとに tick から呼ぶ）
    void apply_metro() {
        if (mtrips.empty()) return;
        long tod = ((long)std::llround(wall) + 9 * 3600) % 86400; if (tod < 0) tod += 86400;
        std::map<std::string, Train> old;
        std::vector<Train> next;
        for (auto& t : trains) {
            if (t.sched) old[t.id] = t;
            else if (t.live) next.push_back(t);
            else if (t.line >= 0 && t.line < (int)sched_line.size() && sched_line[t.line]) continue;   // ダイヤ路線の仮列車は捨てる
            else next.push_back(t);
        }
        for (auto& tr : mtrips) {
            if (tr.cal != metro_cal) continue;
            const auto& st = tr.stops;
            long tt = -1;
            for (long cand : {tod, tod + 86400}) if (cand >= st.front().second && cand <= st.back().second) { tt = cand; break; }
            if (tt < 0) continue;
            int i = 0; while (i + 1 < (int)st.size() && st[i + 1].second <= tt) ++i;
            if (i + 1 >= (int)st.size()) continue;
            int oiA = st[i].first, oiB = st[i + 1].first, secA = st[i].second, secB = st[i + 1].second;
            if (secB <= secA) continue;
            double fpos = oiA + (double)(oiB - oiA) * (tt - secA) / (double)(secB - secA);
            int step = oiB > oiA ? 1 : -1;
            const auto& nodes = mrails[tr.rail].nodes;
            int cell = (int)std::floor(fpos);
            if (cell < 0) cell = 0;
            if (cell + 1 >= (int)nodes.size()) cell = (int)nodes.size() - 2;
            if (cell < 0) continue;
            int na = nodes[cell], nb = nodes[cell + 1];
            if (na < 0 || nb < 0) continue;
            int from_node, to_node; double prog;
            if (step > 0) { from_node = na; to_node = nb; prog = fpos - cell; }
            else { from_node = nb; to_node = na; prog = (cell + 1) - fpos; }
            prog = std::clamp(prog, 0.0, 1.0);
            int line = mrails[tr.rail].line;
            int e = net.edge_of(line, from_node, to_node);
            if (e < 0) continue;
            std::string id = "M" + std::to_string(tr.rail) + ":" + tr.num;
            Train t; auto it = old.find(id);
            if (it != old.end()) t = it->second;
            else { t.sched = true; t.live = true; t.id = id; t.line = line; }
            t.number = tr.num; t.dest = tr.dest; t.asc = tr.asc; t.delay = 0;
            t.edge = e; t.dir = net.edges[e].a == from_node ? 1 : -1;
            t.from = from_node; t.to = to_node;
            int pc = step > 0 ? cell - 1 : cell + 2;   // 進む向きの「1 つ後ろの駅」（尾を伸ばす先）
            t.prev_edge = -1;
            if (pc >= 0 && pc < (int)nodes.size() && nodes[pc] >= 0) {
                int pe = net.edge_of(line, nodes[pc], from_node);
                if (pe >= 0) { t.prev_edge = pe; t.prev_dir = net.edges[pe].a == nodes[pc] ? 1 : -1; }
            }
            double segSec = (secB - secA) / (double)std::max(1, std::abs(oiB - oiA));   // 1 駅ぶんの秒
            t.rate = 1.0 / std::max(20.0, segSec);
            t.p0 = prog; t.t0 = wall; t.confirmed = false;
            t.t = t.dir > 0 ? prog : 1 - prog;
            next.push_back(std::move(t));
        }
        trains.swap(next);
        metro_count = 0; for (auto& t : trains) metro_count += t.sched;
    }

    double live_p(const Train& t) const { return std::clamp(t.p0 + (wall - t.t0) * t.rate, 0.0, t.confirmed ? 1.0 : 0.97); }

    // 駅 n に止める。置く辺は、なるべくいまの辺（着いた区間）に
    void place_at(Train& t, int n) {
        int e = -1;
        auto touches = [&](int f) { return f >= 0 && f < (int)net.edges.size() && net.edges[f].line == t.line && (net.edges[f].a == n || net.edges[f].b == n); };
        // 尾が来た向きに伸びるよう、「1 つ後ろの駅」からの辺に置く（わからなければ、いまの辺＝着いた区間）
        int b = odpt.behind(net, t.line, n, t.asc);
        if (b >= 0) e = net.edge_of(t.line, b, n);
        if (e < 0 && touches(t.edge)) e = t.edge;
        if (e < 0) for (int f : net.lines[t.line].edges) if (touches(f)) { e = f; break; }
        if (e < 0) return;
        t.edge = e; t.dir = net.edges[e].b == n ? 1 : -1; t.t = t.dir > 0 ? 1 : 0;
        t.from = n; t.to = -1; t.dwell = 1; t.confirmed = false;
    }
    // a → b を走り始める。いまの辺が a につながっていれば、それを尾のための 1 つ前の区間にする
    bool begin_move(Train& t, int a, int b, double p0, double rate, bool confirmed) {
        int e = net.edge_of(t.line, a, b);
        if (e < 0) return false;
        if (t.edge >= 0 && t.edge != e && (net.edges[t.edge].a == a || net.edges[t.edge].b == a)) { t.prev_edge = t.edge; t.prev_dir = net.edges[t.edge].b == a ? 1 : -1; }
        else {
            int b = odpt.behind(net, t.line, a, t.asc);   // 初めて見た列車も、後ろの駅がわかれば尾をそこまで伸ばせる
            int pe = b >= 0 ? net.edge_of(t.line, b, a) : -1;
            t.prev_edge = pe;
            if (pe >= 0) t.prev_dir = net.edges[pe].b == a ? 1 : -1;
        }
        t.edge = e; t.dir = net.edges[e].a == a ? 1 : -1;
        t.from = a; t.to = b; t.p0 = p0; t.t0 = wall; t.rate = rate; t.confirmed = confirmed; t.dwell = 0;
        t.t = t.dir > 0 ? p0 : 1 - p0;
        return true;
    }
    // odpt:Train の JSON を反映する。戻り値は地図に載った ODPT の列車の数
    int apply_odpt(const std::string& json_text) {
        if (odpt.empty()) return 0;
        auto obs = parse_odpt_trains(json_text, odpt);
        live_line.assign(net.lines.size(), 0);
        for (auto& kv : odpt.railway) live_line[kv.second] = 1;
        std::map<std::string, Train> old;
        std::vector<Train> next;
        for (auto& t : trains) {
            if (t.sched) next.push_back(t);            // メトロのダイヤ列車は保つ（apply_metro が管理する）
            else if (t.live) old[t.id] = t;
            else if (!live_line[t.line]) next.push_back(t);
        }
        for (auto& o : obs) {
            auto it = old.find(o.id);
            bool known = it != old.end() && it->second.line == o.line;
            Train t;
            if (known) t = it->second;
            else { t.live = true; t.id = o.id; t.line = o.line; t.edge = -1; }
            t.number = o.number; t.dest = o.dest; t.delay = o.delay; t.asc = o.asc;
            if (o.to >= 0) {
                if (!(known && t.from == o.from && t.to == o.to)) {
                    double r = odpt.run_time(o.line, o.from, o.to, 90);
                    bool ok = known && t.to < 0 && t.from == o.from
                        ? begin_move(t, o.from, o.to, 0, 1 / std::max(20.0, r - 15), false)   // 駅にいた列車が出た（この 30 秒のどこかで）
                        : begin_move(t, o.from, o.to, std::min(0.5, 15 / r), 1 / r, false);   // 初めて見た／途中が飛んだ
                    if (!ok) place_at(t, o.from);
                }
            } else if (known && t.to == o.from) {   // 走ってきて着いた
                double p = live_p(t);
                t.p0 = p; t.t0 = wall; t.rate = std::max(t.rate, (1 - p) / 6); t.confirmed = true;
            } else if (known && t.to < 0 && t.from != o.from && net.edge_of(o.line, t.from, o.from) >= 0) {   // あいだを見ないまま隣の駅へ
                double r = odpt.run_time(o.line, t.from, o.from, 90);
                begin_move(t, t.from, o.from, 0, 1 / std::min(r, 25.0), true);
            } else if (!(known && t.to < 0 && t.from == o.from)) {
                place_at(t, o.from);
            }
            if (t.edge < 0) continue;
            next.push_back(t);
        }
        trains.swap(next);
        live_count = 0;
        for (auto& t : trains) live_count += t.live && !t.sched;
        odpt_state = 1; odpt_time = wall; dirty_ui = true;
        return live_count;
    }
    // 画面上の点に近い列車（px）。無ければ -1
    int pick_train(double sx, double sy, double maxd = 12) const {
        int best = -1; double bd = maxd;
        for (size_t i = 0; i < trains.size(); ++i) {
            V2 p = train_pos(trains[i]);
            double d = std::hypot(p.x - sx, p.y - sy);
            if (d < bd) { bd = d; best = (int)i; }
        }
        return best;
    }

    // 辺の上の位置（画面座標）
    V2 train_pos(const Train& t) const {
        auto shape = edge_shape(t.edge);
        double L = poly_len(shape), s = std::clamp(t.t, 0.0, 1.0) * L, acc = 0;
        P2 p = shape.back();
        for (size_t i = 1; i < shape.size(); ++i) {
            double sl = len(shape[i] - shape[i - 1]);
            if (acc + sl >= s) { p = shape[i - 1] + (shape[i] - shape[i - 1]) * (sl > 0 ? (s - acc) / sl : 0); break; }
            acc += sl;
        }
        V2 q = to_screen(p);
        // 横に並べた線に合わせてずらす
        const Edge& E = net.edges[t.edge];
        float d = (float)edge_offset(t.edge) * line_width() * 1.1f;
        if (d != 0 && shape.size() >= 2) {
            V2 a = to_screen(shape.front()), b = to_screen(shape.back());
            float dx = b.x - a.x, dy = b.y - a.y, Ln = std::sqrt(dx * dx + dy * dy);
            if (E.a > E.b) d = -d;
            if (Ln > 0) { q.x -= dy / Ln * d; q.y += dx / Ln * d; }
        }
        return q;
    }

    // 区間 e の上の位置 t（0..1）の画面座標
    V2 at_on(int e, double t) const { Train tmp; tmp.edge = e; tmp.t = t; return train_pos(tmp); }

    // 尾：列車の位置から、たどってきた向きへ length px ぶんの点の列（先頭が列車）
    std::vector<V2> tail_points(const Train& t, float length, int n) const {
        std::vector<V2> out;
        V2 head = train_pos(t);
        out.push_back(head);
        // 画面上の区間の長さ（だいたい）で、t を戻す量を決める
        auto edge_px = [&](int e) { V2 a = at_on(e, 0), b = at_on(e, 1); return std::max(1.f, std::hypot(b.x - a.x, b.y - a.y) * 1.15f); };
        float per = length / n;
        int e = t.edge, d = t.dir;
        double pos = t.t;
        bool used_prev = false;
        for (int k = 1; k <= n; ++k) {
            double back = per / edge_px(e);
            double np = pos - d * back;
            if (np < 0 || np > 1) {
                if (used_prev || t.prev_edge < 0) break;
                // 1 つ前の区間へ：その区間の終わり（列車が出てきた側）から続ける
                double over = d > 0 ? -np : np - 1;
                e = t.prev_edge; d = t.prev_dir; used_prev = true;
                pos = d > 0 ? 1 - over * edge_px(t.edge) / edge_px(e) : over * edge_px(t.edge) / edge_px(e);
                pos = std::clamp(pos, 0.0, 1.0);
            } else pos = np;
            out.push_back(at_on(e, pos));
        }
        return out;
    }

    // ---- きょうのダイヤ（平日／土休） ----
    // 日本時間の日付。ダイヤの 1 日は朝 3 時に切り替わるとみなす（真夜中すぎの列車は前の日のダイヤ）
    static int holiday_cal(double wall) {
        long long days = (long long)std::floor((wall + 9 * 3600 - 3 * 3600) / 86400);
        int wd = (int)(((days + 4) % 7 + 7) % 7);   // 0 = 日曜（1970-01-01 は木曜）
        if (wd == 0 || wd == 6) return 1;
        // 日付（days から年月日）
        long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
        unsigned doe = (unsigned)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
        int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
        int y = (int)(yoe + era * 400) + (m <= 2);
        static const int HOL[] = {   // 祝日・振替休日（2026〜2027）
            20260101, 20260112, 20260211, 20260223, 20260320, 20260429, 20260503, 20260504, 20260505, 20260506,
            20260720, 20260811, 20260921, 20260922, 20260923, 20261012, 20261103, 20261123,
            20270101, 20270111, 20270211, 20270223, 20270321, 20270322, 20270429, 20270503, 20270504, 20270505,
            20270719, 20270811, 20270920, 20270923, 20271011, 20271103, 20271123};
        int key = y * 10000 + m * 100 + d;
        for (int h : HOL) if (h == key) return 1;
        return 0;
    }

    // ---- 画面の部品 ----
    void odpt_failed() { odpt_state = 2; dirty_ui = true; }
    float px(float v) const { return v * (float)ui; }
    int fpx(float v) const { return std::max(8, (int)std::lround(v * ui)); }
    bool wide() const { return W / ui >= 700; }   // 凡例を出す広さ（スマホの縦では出さない）
    static std::string hhmmss(double t) {
        long long s = (long long)std::floor(t + 9 * 3600) % 86400; if (s < 0) s += 86400;
        char b[16]; snprintf(b, sizeof b, "%02d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
        return b;
    }
    void text(Canvas& cv, float x, float y, const std::string& s, float size, Rgb c) { font.draw(cv, x, y, s, fpx(size), c, {0, 0, 0}, 0); }
    float tw(const std::string& s, float size) { return font.width(s, fpx(size)); }
    void panel(Canvas& cv, float x0, float y0, float x1, float y1) {
        round_rect(cv, x0, y0, x1, y1, px(12), {0.07f, 0.09f, 0.14f}, 0.84f);
        panels.push_back({x0, y0, x1, y1, PANEL});
    }
    void button(Canvas& cv, float x, float y, const std::string& s, int id, bool on) {
        float w = tw(s, 13.5f) + px(22), h = px(30);
        round_rect(cv, x, y, x + w, y + h, px(8), on ? Rgb{0.35f, 0.69f, 1.0f} : Rgb{0.16f, 0.2f, 0.28f}, 0.95f);
        text(cv, x + px(11), y + h / 2 + px(13.5f) * 0.36f, s, 13.5f, on ? Rgb{0.02f, 0.07f, 0.12f} : Rgb{0.9f, 0.93f, 0.97f});
        buttons.push_back({x, y, x + w, y + h, id});
    }
    // 路線の丸い印（色の丸に記号の文字）
    void line_mark(Canvas& cv, float cx, float cy, int l) {
        const auto& L = net.lines[l];
        disc(cv, {cx, cy}, px(11), hex(L.color));
        std::string c(1, L.code);
        font.draw(cv, cx - tw(c, 11.5f) / 2, cy + px(11.5f) * 0.36f, c, fpx(11.5f), {1, 1, 1}, {0.05f, 0.05f, 0.08f}, 1.2f);
    }
    // 文字の並び（色つきの切れ目）を幅 maxw で折り返して書く。y は 1 行目のベースライン。戻り値は使った高さ（cv が null なら測るだけ）
    struct Run { std::string s; Rgb c; };
    float wrap(Canvas* cv, float x, float y, float maxw, const std::vector<Run>& runs, float size, float lh) {
        static const std::string NO_HEAD = "、。，．）」』・／";   // 行の頭に来ないほうがよい文字
        float pen = 0; int line = 0, sz = fpx(size);
        for (auto& r : runs)
            for (size_t i = 0; i < r.s.size();) {
                size_t j = i; next_cp(r.s, j);
                auto word = [&](size_t k) { return k < r.s.size() && (std::isalnum((unsigned char)r.s[k]) || r.s[k] == '.' || r.s[k] == '-'); };
                if (word(i)) while (word(j)) ++j;   // 英数字の単語は途中で折らない
                std::string ch = r.s.substr(i, j - i); i = j;
                if (ch == "\n") { pen = 0; ++line; continue; }
                float w = font.width(ch, sz);
                if (pen > 0 && pen + w > maxw && NO_HEAD.find(ch) == std::string::npos) { pen = 0; ++line; }
                if (pen == 0 && ch == " ") continue;
                if (cv) font.draw(*cv, x + pen, y + line * lh, ch, sz, r.c, {0, 0, 0}, 0);
                pen += w;
            }
        return (line + 1) * lh;
    }

    void draw_ui(Canvas& cv) {
        buttons.clear(); panels.clear();
        if (!font.ok) return;
        const Rgb fg{0.9f, 0.93f, 0.97f}, muted{0.55f, 0.6f, 0.68f};
        // 左上：題名・いまの様子・ボタン
        std::string st = std::to_string(net.lines.size()) + " 路線・" + std::to_string(net.nodes.size()) + " 駅";
        if (odpt_state == 1) st += " ・ 都営の列車 " + std::to_string(live_count) + " 本（" + hhmmss(odpt_time) + " 更新）";
        else if (odpt_state == 2) st += " ・ 都営の列車を取れませんでした。30 秒後にもう一度";
        const std::string title = "東京の地下鉄 列車マップ", b1 = "実際の地図", b2 = "路線図", b3 = "全体を表示";
        float bw = tw(b1, 13.5f) + tw(b2, 13.5f) + tw(b3, 13.5f) + px(22 * 3 + 6 * 2);
        float inner = std::max({tw(title, 16), bw, std::min(tw(st, 11.5f), px(420))});
        inner = std::min(inner, (float)W - px(32 + 28));
        const float x0 = px(16), y0 = px(16), slh = px(17);
        float sth = wrap(nullptr, 0, 0, inner, {{st, muted}}, 11.5f, slh);
        float hpan = px(12) + px(20) + px(4) + sth + px(8) + px(30) + px(14);
        panel(cv, x0, y0, x0 + inner + px(28), y0 + hpan);
        float y = y0 + px(12) + px(16);
        text(cv, x0 + px(14), y, title, 16, fg);
        y += px(4) + px(16);
        wrap(&cv, x0 + px(14), y, inner, {{st, muted}}, 11.5f, slh);
        y += sth - slh + px(12);
        bool sch = target > 0.5;
        button(cv, x0 + px(14), y, b1, BTN_GEO, !sch);
        button(cv, buttons.back().x1 + px(6), y, b2, BTN_SCH, sch);
        button(cv, buttons.back().x1 + px(6), y, b3, BTN_FIT, false);
        // 右上：路線の凡例（広い画面だけ）
        if (wide()) {
            float nw = 0;
            for (auto& L : net.lines) nw = std::max(nw, tw(L.name, 12.5f));
            float rowh = px(26), lx1 = W - px(16), lx0 = lx1 - (nw + px(12 + 22 + 8 + 14)), ly0 = px(16);
            int rows = std::min((int)net.lines.size(), (int)((H - px(32 + 20)) / rowh));
            if (rows > 0) {
                panel(cv, lx0, ly0, lx1, ly0 + px(8) + rows * rowh + px(8));
                for (int i = 0; i < rows; ++i) {
                    float cy = ly0 + px(8) + rowh * (i + 0.5f);
                    line_mark(cv, lx0 + px(12 + 11), cy, i);
                    text(cv, lx0 + px(12 + 22 + 8), cy + px(12.5f) * 0.36f, net.lines[i].name, 12.5f, fg);
                }
            }
        }
        // 左下：説明と出典
        std::vector<Run> note = {
            {"都営 4 路線", fg}, {"（浅草・三田・新宿・大江戸）の列車は、いま実際に走っている列車の位置（ODPT、約 30 秒ごとに更新）。", muted},
            {"メトロ 9 路線", fg}, {"は列車位置が公開されていないので、時刻表（ODPT）どおりの位置で走らせています（ダイヤ上の位置。遅延は反映しません）。"
                                  "いずれも駅と駅のあいだは補間しています。光る頭と尾が列車で、尾の反対側へ進んでいます。"
                                  "赤く光る玉は現在地（都内にいるときだけ）。ドラッグで移動、ホイール・2 本指で拡大縮小、駅や列車をタップで名前・行き先。\n", muted},
            {"列車の位置（都営）：公共交通オープンデータセンター（ODPT）を通じて東京都交通局が提供するデータを使用。内容の正確さ・完全さは保証されません／"
             "線路と駅：「国土数値情報（鉄道データ N02）」、東京都の外周と 23 区の区界：「国土数値情報（行政区域データ N03）」（いずれも国土交通省）を加工して作成（CC BY 4.0）／"
             "文字：Noto Sans JP（SIL OFL 1.1）", {0.47f, 0.52f, 0.6f}}};
        float nw = std::min((float)W - px(32 + 24), px(560)), lh = px(16.5f);
        float nh = wrap(nullptr, 0, 0, nw, note, 11, lh) + px(16);
        float ny1 = H - px(16), ny0 = ny1 - nh;
        if (ny0 > y0 + hpan + px(8)) {   // 上のパネルと重なるほど狭い画面では出さない
            panel(cv, x0, ny0, x0 + nw + px(24), ny1);
            wrap(&cv, x0 + px(12), ny0 + px(8) + lh * 0.75f, nw, note, 11, lh);
        }
    }

    // 吹き出し：列車（路線・行き先・列車番号・遅れ）か駅（名前と路線）
    void draw_tip(Canvas& cv) {
        if (hover_x < 0 || !font.ok || hit(hover_x, hover_y)) return;
        struct Ln { int mark; std::string s; Rgb c; };
        std::vector<Ln> lines;
        const Rgb fg{1, 1, 1}, sub{0.75f, 0.8f, 0.88f};
        int ti = pick_train(hover_x, hover_y, 10 * ui);
        if (ti >= 0) {
            const Train& t = trains[ti];
            bool named = t.live || t.sched;   // 行き先・列車番号を出せる（都営の実列車 or メトロのダイヤ）
            lines.push_back({t.line, net.lines[t.line].name, fg});
            if (named) lines.push_back({-1, (t.dest.empty() ? std::string() : t.dest + " 行き・") + t.number, sub});
            std::string d = t.sched ? "ダイヤ上の位置" : !t.live ? "仮の列車" : t.delay >= 60 ? std::to_string((int)std::lround(t.delay / 60)) + " 分遅れ" : "遅れなし";
            lines.push_back({-1, d, sub});
        } else {
            int n = pick(hover_x, hover_y, 14 * ui);
            if (n < 0) return;
            lines.push_back({-1, net.nodes[n].name, fg});
            for (int l : net.nodes[n].lines) lines.push_back({l, net.lines[l].name, sub});
        }
        float w = 0;
        for (auto& l : lines) w = std::max(w, tw(l.s, 13) + (l.mark >= 0 ? px(30) : 0));
        w += px(20);
        float rowh = px(24), h = px(10) + rowh * lines.size();
        float x = std::min((float)hover_x + px(14), W - w - px(4)), y = std::min((float)hover_y + px(14), H - h - px(4));
        round_rect(cv, x, y, x + w, y + h, px(10), {0.07f, 0.09f, 0.14f}, 0.94f);
        for (size_t i = 0; i < lines.size(); ++i) {
            float cy = y + px(5) + rowh * (i + 0.5f), tx = x + px(10);
            if (lines[i].mark >= 0) { line_mark(cv, tx + px(11), cy, lines[i].mark); tx += px(30); }
            text(cv, tx, cy + px(13) * 0.36f, lines[i].s, 13, lines[i].c);
        }
    }

    // ---- 操作 ----
    void hover(double x, double y) { hover_x = x; hover_y = y; }
    // その点のボタン（id）。パネルの上なら PANEL、どちらでもなければ 0
    int hit(double x, double y) const {
        for (auto& b : buttons) if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1) return b.id;
        for (auto& b : panels) if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1) return PANEL;
        return 0;
    }
    int click(double x, double y) {
        int id = hit(x, y);
        if (id == BTN_GEO) { set_mode(0); dirty_ui = true; }
        else if (id == BTN_SCH) { set_mode(1); dirty_ui = true; }
        else if (id == BTN_FIT) fit();
        return id;
    }

    // ---- 現在地 ----
    // 点 p が多角形の輪たちの内側か（偶奇則）
    static bool inside(const std::vector<std::vector<P2>>& rings, P2 p) {
        bool in = false;
        for (auto& r : rings)
            for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
                if ((r[i].y > p.y) != (r[j].y > p.y) && p.x < (r[j].x - r[i].x) * (p.y - r[i].y) / (r[j].y - r[i].y) + r[i].x) in = !in;
        return in;
    }
    // 東京都（本土）の中か。外周がまだ読めていなければ、おおまかな矩形で
    bool in_tokyo(double lat, double lon) const {
        if (!admin_pref.empty()) return inside(admin_pref, mercator(lon, lat));
        return lon > 138.94 && lon < 139.93 && lat > 35.50 && lat < 35.90;
    }
    // 戻り値は 1 = 都内（赤い丸を出す）、0 = 都の外（出さない）
    int set_here(double lat, double lon) {
        here_on = in_tokyo(lat, lon);
        here = mercator(lon, lat);
        return here_on;
    }
    void clear_here() { here_on = false; }
    // 画面に出す位置。路線図風へ変形するときは、近くの駅（4 つ、近いほど重く）がずれたぶんだけ一緒にずらす
    P2 here_pos() const {
        double e = ease();
        if (e <= 0 || net.nodes.empty()) return here;
        std::vector<std::pair<double, int>> d;
        for (size_t n = 0; n < net.nodes.size(); ++n) { P2 v = net.nodes[n].geo - here; d.push_back({dot(v, v), (int)n}); }
        size_t k = std::min<size_t>(4, d.size());
        std::partial_sort(d.begin(), d.begin() + k, d.end());
        P2 off{0, 0}; double wsum = 0;
        for (size_t i = 0; i < k; ++i) {
            double w = 1.0 / (d[i].first + 0.01);   // 0.1 km ほどより近ければ、ほぼその駅と同じだけ
            off = off + (sch_km[d[i].second] - net.nodes[d[i].second].geo) * w; wsum += w;
        }
        return here + off * (e / wsum);
    }
    // 現在地の印：列車・バスと同じ光る玉（赤）。ゆっくり息をするように光の大きさが変わる
    void draw_here(Canvas& cv) {
        if (!here_on) return;
        V2 p = to_screen(here_pos());
        if (p.x < -40 || p.y < -40 || p.x > W + 40 || p.y > H + 40) return;
        const Rgb red{1.0f, 0.18f, 0.15f};
        float breathe = 0.5f + 0.5f * (float)std::sin(wall * 2.5);   // 0..1、約 2.5 秒で 1 回
        glow_dot(cv, p, 26 * (float)ui + 8 * (float)ui * breathe, red, 0.75f + 0.25f * breathe);   // 外の光
        glow_dot(cv, p, 12 * (float)ui, red, 1.0f);                                    // 内の光
        disc(cv, p, 8 * (float)ui, {0.08f, 0.01f, 0.01f}, 0.85f);                         // 細い暗い縁（明るい線の上でも見えるように）
        disc(cv, p, 6.5f * (float)ui, red, 1.f);                                        // 芯（赤のまま）
        glow_dot(cv, p, 4.5f * (float)ui, {1, 0.85f, 0.8f}, 0.9f);                       // 真ん中の白いきらめき
    }

    const uint8_t* render() {
        if (dirty || mode != last_mode) { draw_base(); dirty = false; last_mode = mode; dirty_ui = true; }
        if (dirty_ui) { info.px = base.px; draw_ui(info); dirty_ui = false; }
        frame.px = info.px;
        const float lw = line_width();
        for (auto& t : trains) {
            V2 p = train_pos(t);
            if (p.x < -60 || p.y < -60 || p.x > W + 60 || p.y > H + 60) continue;
            Rgb c = hex(net.lines[t.line].color);
            // 流れ星の尾：後ろへ行くほど細く・暗く
            // 止まっているあいだも、来た向きがわかるように尾を出す（少し短く）
            {
                auto tail = tail_points(t, lw * (t.dwell > 0 ? 12.f : 18.f), 20);
                for (size_t k = tail.size(); k-- > 1;) {
                    float f = 1.f - (float)k / tail.size();
                    polyline(frame, {tail[k], tail[k - 1]}, lw * (0.3f + 1.7f * f), mix(c, {1, 1, 1}, 0.45f * f), 0.9f * f * f, true);
                }
            }
            // 頭：駅（白い点）と見分けられるよう、大きめの白い芯＋路線カラーの光
            glow_dot(frame, p, lw * 5.5f, c, 1.0f);
            disc(frame, p, lw * 1.35f, mix(c, {1, 1, 1}, 0.75f), 1.f);
            glow_dot(frame, p, lw * 1.6f, {1, 1, 1}, 0.8f);
        }
        draw_here(frame);   // 列車の上に（光は足し算なので、近くの列車は透けて見える）
        // パネルは列車より上に：パネルの四角だけ、とっておいた絵（info）に戻す
        for (auto& b : panels) {
            int x0 = std::max(0, (int)std::floor(b.x0)), x1 = std::min(W, (int)std::ceil(b.x1));
            if (x1 <= x0) continue;
            for (int y = std::max(0, (int)std::floor(b.y0)); y < std::min(H, (int)std::ceil(b.y1)); ++y)
                std::copy(info.px.begin() + ((size_t)y * W + x0) * 3, info.px.begin() + ((size_t)y * W + x1) * 3, frame.px.begin() + ((size_t)y * W + x0) * 3);
        }
        draw_tip(frame);
        frame.to_rgba(rgba.data());
        return rgba.data();
    }

    // 画面上の点に近い駅（px）。無ければ -1
    int pick(double sx, double sy, double maxd = 14) const {
        int best = -1; double bd = maxd;
        for (size_t n = 0; n < net.nodes.size(); ++n) {
            V2 p = to_screen(node_pos((int)n));
            double d = std::hypot(p.x - sx, p.y - sy);
            if (d < bd) { bd = d; best = (int)n; }
        }
        return best;
    }
};

}  // namespace mm
