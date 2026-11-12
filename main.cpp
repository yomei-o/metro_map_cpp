// metro_map — ネイティブで地図を PNG に描く（WASM と同じ metro_map.hpp）
//   metro_map [data/network.txt] [fonts/NotoSansJP-Bold-subset.ttf] [out_prefix] [W] [H] [data/odpt_toei.txt train1.json train2.json ...]
//   列車の JSON（odpt:Train）を渡すと、30 秒おきに届いたものとして順に反映し、その都度の様子を表示する
//   → <out>_geo.png（実際の地図風）, <out>_mid.png（変形の途中）, <out>_sch.png（路線図風）
#define _CRT_SECURE_NO_WARNINGS
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "metro_map.hpp"

// stb の実装はここで 1 回だけ（metro_map.hpp が宣言を先に取り込んでいるので、後に置く）
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_truetype.h"
#include "stb_image_write.h"

static std::string read_all(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main(int argc, char** argv) {
    std::string net_p = argc > 1 ? argv[1] : "data/network.txt", font_p = argc > 2 ? argv[2] : "fonts/NotoSansJP-Bold-subset.ttf";
    std::string out = argc > 3 ? argv[3] : "out/map";
    int W = argc > 4 ? atoi(argv[4]) : 1280, H = argc > 5 ? atoi(argv[5]) : 800;
    std::string text = read_all(net_p), font = read_all(font_p);
    if (text.empty()) { fprintf(stderr, "cannot read %s\n", net_p.c_str()); return 1; }
    mm::Map m;
    auto t0 = std::chrono::steady_clock::now();
    if (!m.load(text, (const unsigned char*)font.data(), font.size())) { fprintf(stderr, "load: %s\n", m.net.error.c_str()); return 1; }
    { std::string a = read_all("data/admin_tokyo.txt"); if (!a.empty()) printf("admin rings %d\n", m.load_admin(a)); }   // 東京都の外周と 23 区
    auto t1 = std::chrono::steady_clock::now();
    printf("lines %zu, stations %zu, edges %zu, trains %zu (load + layout %.0f ms)\n", m.net.lines.size(), m.net.nodes.size(), m.net.edges.size(),
           m.trains.size(), std::chrono::duration<double, std::milli>(t1 - t0).count());
    for (auto& L : m.net.lines) printf("  %c %s: %zu stations, %zu edges\n", L.code, L.name.c_str(), L.nodes.size(), L.edges.size());
    m.resize(W, H);
    m.fit();
    for (int k = 0; k < 60; ++k) m.tick(0.5);   // 列車を少し走らせて、尾が出るように
    if (argc > 6) {
        int bad = m.load_odpt(read_all(argv[6]));
        printf("odpt: %zu railways, %zu stations, %zu runs, %d unmatched\n", m.odpt.railway.size(), m.odpt.station.size(), m.odpt.run_l.size(), bad);
        m.wall = 1.7e9;
        for (int a = 7; a < argc; ++a) {
            int n = m.apply_odpt(read_all(argv[a]));
            int moving = 0, at = 0;
            for (auto& t : m.trains) if (t.live) (t.to >= 0 ? moving : at)++;
            printf("  %s: %d live trains (%d moving, %d at station), %zu total\n", argv[a], n, moving, at, m.trains.size());
            for (int k = 0; k < (a + 1 < argc ? 60 : 30); ++k) { m.wall += 0.5; m.tick(0.5); }
        }
    }
    struct Shot { const char* name; double mode; };
    for (Shot s : {Shot{"geo", 0.0}, Shot{"mid", 0.5}, Shot{"sch", 1.0}}) {
        m.mode = m.target = s.mode;
        m.dirty = true;
        auto a = std::chrono::steady_clock::now();
        const uint8_t* px = m.render();
        auto b = std::chrono::steady_clock::now();
        const uint8_t* px2 = m.render();   // 2 回目は地図をとっておいた分だけ（列車のみ）
        auto c = std::chrono::steady_clock::now();
        (void)px;
        std::string f = out + "_" + s.name + ".png";
        stbi_write_png(f.c_str(), W, H, 4, px2, W * 4);
        printf("%s: full %.0f ms, trains only %.1f ms -> %s\n", s.name, std::chrono::duration<double, std::milli>(b - a).count(),
               std::chrono::duration<double, std::milli>(c - b).count(), f.c_str());
    }
    return 0;
}
