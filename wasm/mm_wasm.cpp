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
EMSCRIPTEN_KEEPALIVE void mm_speed(double s) { g.sim_speed = s; }
EMSCRIPTEN_KEEPALIVE void mm_tick(double dt) { g.tick(dt); }
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
// 画面上の点に近い駅: {"name":"大手町","lines":["M","T",...]} または {}
EMSCRIPTEN_KEEPALIVE const char* mm_pick(double x, double y) {
    int n = g.pick(x, y);
    if (n < 0) return (g_json = "{}").c_str();
    g_json = "{\"name\":\"" + g.net.nodes[n].name + "\",\"lines\":[";
    for (size_t k = 0; k < g.net.nodes[n].lines.size(); ++k) g_json += std::string(k ? "," : "") + "\"" + g.net.lines[g.net.nodes[n].lines[k]].code + "\"";
    return (g_json += "]}").c_str();
}
}
