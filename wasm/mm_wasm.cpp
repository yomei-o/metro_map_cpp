// WASM の入口。ページが network.txt とフォントを渡し、毎フレーム tick → render を呼んで canvas に貼る。
// ネイティブ（main.cpp）と同じ metro_map.hpp。
//   build: sh wasm/build.sh → docs/mm.js, docs/mm.wasm
#include "metro_map.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <emscripten/emscripten.h>
#include <string>

static mm::Map g;
static std::string g_json;

extern "C" {
EMSCRIPTEN_KEEPALIVE int mm_init(const char* text, const unsigned char* font, int font_n) {
    return g.load(text, font, (size_t)font_n) ? (int)g.net.nodes.size() : 0;
}
EMSCRIPTEN_KEEPALIVE void mm_resize(int w, int h) { g.resize(w, h); }
EMSCRIPTEN_KEEPALIVE void mm_fit() { g.fit(); }
EMSCRIPTEN_KEEPALIVE void mm_pan(double dx, double dy) { g.pan(dx, dy); }
EMSCRIPTEN_KEEPALIVE void mm_zoom(double f, double x, double y) { g.zoom(f, x, y); }
EMSCRIPTEN_KEEPALIVE void mm_mode(int m) { g.set_mode(m); }
EMSCRIPTEN_KEEPALIVE void mm_ui(double u) { g.ui = u; g.dirty = true; }
EMSCRIPTEN_KEEPALIVE void mm_tick(double dt, double wall) { g.wall = wall; g.tick(dt); }
// ODPT: 駅と所要時間の表（data/odpt_toei.txt）を読む。戻り値は地図に結びつかなかった行の数
EMSCRIPTEN_KEEPALIVE int mm_odpt_table(const char* text) { return g.load_odpt(text); }
// 東京都の外周と 23 区の区界（data/admin_tokyo.txt）。戻り値は読み込んだ輪の数。実際の地図のときだけ薄く描く
EMSCRIPTEN_KEEPALIVE int mm_admin(const char* text) { return g.load_admin(text); }
// ODPT: odpt:Train の JSON を反映する。戻り値は地図に載った列車の数
EMSCRIPTEN_KEEPALIVE int mm_odpt(const char* json, double wall) { g.wall = wall; return g.apply_odpt(json); }
EMSCRIPTEN_KEEPALIVE const uint8_t* mm_render() { return g.render(); }
// 路線の一覧（凡例用）: [{"code":"G","name":"銀座線","color":"FF9500"},...]
EMSCRIPTEN_KEEPALIVE const char* mm_lines() {
    g_json = "[";
    char b[16];
    for (size_t i = 0; i < g.net.lines.size(); ++i) {
        auto& L = g.net.lines[i];
        snprintf(b, sizeof b, "%06X", L.color);
        g_json += std::string(i ? "," : "") + "{\"code\":\"" + L.code + "\",\"name\":\"" + L.name + "\",\"color\":\"" + b + "\"}";
    }
    return (g_json += "]").c_str();
}
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }
// 画面上の点に近い列車または駅
//   {"train":"1579T","line":"I","dest":"西高島平","delay":0,"live":1} / {"name":"大手町","lines":["M","T",...]} / {}
EMSCRIPTEN_KEEPALIVE const char* mm_pick(double x, double y) {
    int ti = g.pick_train(x, y, 10 * g.ui);
    if (ti >= 0) {
        auto& t = g.trains[ti];
        char b[64]; snprintf(b, sizeof b, ",\"delay\":%d,\"live\":%d}", (int)t.delay, t.live ? 1 : 0);
        return (g_json = "{\"train\":\"" + esc(t.number) + "\",\"line\":\"" + std::string(1, g.net.lines[t.line].code) + "\",\"dest\":\"" + esc(t.dest) + "\"" + b).c_str();
    }
    int n = g.pick(x, y);
    if (n < 0) return (g_json = "{}").c_str();
    g_json = "{\"name\":\"" + g.net.nodes[n].name + "\",\"lines\":[";
    for (size_t k = 0; k < g.net.nodes[n].lines.size(); ++k) g_json += std::string(k ? "," : "") + "\"" + g.net.lines[g.net.nodes[n].lines[k]].code + "\"";
    return (g_json += "]}").c_str();
}
}
