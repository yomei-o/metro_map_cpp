// WASM の入口。ページは network.txt・フォント・ODPT の JSON・指やマウスの動き・現在地を渡すだけで、
// 画面（地図・列車・パネル・ボタン・凡例・吹き出し・説明）はすべてここ（metro_map.hpp）で描く。
// ネイティブ（main.cpp）と同じ metro_map.hpp。
//   build: sh wasm/build.sh → docs/mm.js, docs/mm.wasm
#include "metro_map.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <emscripten/emscripten.h>

static mm::Map g;

extern "C" {
EMSCRIPTEN_KEEPALIVE int mm_init(const char* text, const unsigned char* font, int font_n) {
    return g.load(text, font, (size_t)font_n) ? (int)g.net.nodes.size() : 0;
}
EMSCRIPTEN_KEEPALIVE void mm_resize(int w, int h, double ui) { g.ui = ui; g.resize(w, h); }
EMSCRIPTEN_KEEPALIVE void mm_fit() { g.fit(); }
EMSCRIPTEN_KEEPALIVE void mm_pan(double dx, double dy) { g.pan(dx, dy); }
EMSCRIPTEN_KEEPALIVE void mm_zoom(double f, double x, double y) { g.zoom(f, x, y); }
EMSCRIPTEN_KEEPALIVE void mm_tick(double dt, double wall) { g.wall = wall; g.tick(dt); }
EMSCRIPTEN_KEEPALIVE const uint8_t* mm_render() { return g.render(); }
// ODPT: 駅と所要時間の表（data/odpt_toei.txt）を読む。戻り値は地図に結びつかなかった行の数
EMSCRIPTEN_KEEPALIVE int mm_odpt_table(const char* text) { return g.load_odpt(text); }
// 東京都の外周と 23 区の区界（data/admin_tokyo.txt）。戻り値は読み込んだ輪の数。実際の地図のときだけ薄く描く
EMSCRIPTEN_KEEPALIVE int mm_admin(const char* text) { return g.load_admin(text); }
// 東京メトロのダイヤ（data/odpt_metro.txt）。戻り値は列車（trip）の数。平日／土休は C++ が日本時間の日付から決める
EMSCRIPTEN_KEEPALIVE int mm_metro_table(const char* text) { return g.load_metro(text); }
// ODPT: odpt:Train の JSON を反映する。戻り値は地図に載った列車の数。取れなかったときは mm_odpt_fail
EMSCRIPTEN_KEEPALIVE int mm_odpt(const char* json, double wall) { g.wall = wall; return g.apply_odpt(json); }
EMSCRIPTEN_KEEPALIVE void mm_odpt_fail() { g.odpt_failed(); }
// 現在地（緯度・経度）。東京都（本土）の中なら赤く光る玉を出して 1、外なら出さずに 0。mm_here_off で消す
EMSCRIPTEN_KEEPALIVE int mm_here(double lat, double lon) { return g.set_here(lat, lon); }
EMSCRIPTEN_KEEPALIVE void mm_here_off() { g.clear_here(); }
// 操作。mm_hit はその点にボタン（1〜）やパネル（100）があるか（あればドラッグを始めない）
EMSCRIPTEN_KEEPALIVE void mm_hover(double x, double y) { g.hover(x, y); }
EMSCRIPTEN_KEEPALIVE int mm_hit(double x, double y) { return g.hit(x, y); }
EMSCRIPTEN_KEEPALIVE int mm_click(double x, double y) { return g.click(x, y); }
}
