# metro_map_cpp — 東京の地下鉄の列車を、光る地図に走らせる（C++ / WASM）

東京メトロ 9 路線と都営地下鉄 4 路線の線路・駅を、暗い背景に光る線で描きます。列車は流れ星のように尾を引いて走ります。
地図は「実際の地図」と「路線図」の 2 種類で、ボタンを押すと駅がなめらかに動いて変形します。

**デモ**: https://yomei-o.github.io/metro_map_cpp/
（ドラッグで移動、ホイールまたは 2 本指のピンチで拡大縮小、駅をタップ・マウスを乗せると駅名と路線。スマホでも同じ大きさの文字になるよう、画面の倍率に合わせて文字と線を太くしています）

描画はすべて C++（線・光・円・日本語の文字まで自前のソフトウェア描画）です。ブラウザでは WebAssembly にして、できた RGBA を canvas に貼るだけです。
同じ `metro_map.hpp` をネイティブでビルドすると、同じ絵を PNG に書き出せます（`metro_map.exe`）。

> **いまの列車は仮のものです。** 列車の位置は、公共交通オープンデータセンター（ODPT）の列車ロケーション情報で取る予定です。
> 開発者登録の承認待ちのあいだは、それらしく走る仮の列車（区間を約 40 km/h で走り、駅で 25 秒止まる）を使っています。

## しくみ

| ファイル | やること |
|---|---|
| `tools/build_network.py` | 国土数値情報（鉄道データ N02-24）から、地下鉄 13 路線の線路と駅を `data/network.txt` に取り出す |
| `network.hpp` | 経緯度をメルカトル図法に投影し、同じ名前の近い駅を 1 つの乗換駅にまとめる。路線の線路全体を 20 m おきの点の網にして、各駅から網に沿って隣の駅を探し、駅と駅のあいだ（区間）とその線路の形を作る |
| `layout.hpp` | 路線図の配置を計算する。実際の位置に魚眼をかけて都心を広げた位置を出発点に、「区間の向きを 0°/45°/90° に寄せる」「駅の間隔をそろえる」「途中駅でまっすぐ通す」「無関係な駅を離す」を少しずつ同時にかける（乱数なし、毎回同じ結果） |
| `raster.hpp` | なめらかな太線（継ぎ目が濃くならない）、円、光（縮めてぼかして足す）、stb_truetype の日本語（暗い縁取りつき） |
| `metro_map.hpp` | 2 種類の地図の描画と変形、駅名の配置（乗換駅が先、重ならない場所へ）、列車と流れ星の尾 |
| `wasm/` | ブラウザ用の入口とビルド |

- 区間の形は、変形の途中では「実際の線路の形」と「路線図の形」を同じ点の数に取り直して、そのあいだを補間します。
- 同じ 2 駅のあいだを複数の路線が通る区間（有楽町線と副都心線の和光市〜小竹向原など）は、線を横に並べてずらします。
- 線路の形だけでは決められないところは、名前で指定しています（`network.hpp`）。
  - 副都心線が有楽町線の線路を走る区間（N02 では有楽町線にだけ載っている）
  - 方南町支線は中野坂上から入る（線路の網の上では新中野からもつながってしまう）
- 地図（線・駅・駅名・光）は表示が変わったときだけ描き直してとっておき、毎フレームは列車だけを重ねます。
  1280×800 で、地図の描き直しは 40〜80 ms、列車だけなら約 10 ms です（ネイティブ）。

## ビルド

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build/Release/metro_map.exe data/network.txt fonts/NotoSansJP-Bold-subset.ttf out/map 1280 800   # out/map_geo.png, _mid, _sch
sh wasm/build.sh                                                                                 # docs/mm.js, docs/mm.wasm
```

データを作り直すとき（国土数値情報を `data/raw/` に置いて）:

```sh
python tools/build_network.py data/raw/n02/UTF-8/N02-24_RailroadSection.geojson data/raw/n02/UTF-8/N02-24_Station.geojson > data/network.txt
python tools/subset_font.py NotoSansJP[wght].ttf data/network.txt fonts/NotoSansJP-Bold-subset.ttf
```

## 出典とライセンス

- コード: BSD-3-Clause（`LICENSE`）
- 線路と駅: 「国土数値情報（鉄道データ）」（国土交通省）N02-24 を加工して作成（CC BY 4.0）。`data/network.txt`、`docs/network.txt`
- 文字: Noto Sans JP（SIL Open Font License 1.1、`fonts/OFL.txt`）。駅名に使う 350 文字に絞り、太さ 700 に固定したもの
- stb_truetype / stb_image_write: パブリックドメイン（または MIT）
- nlohmann/json 3.12.0（`third_party/nlohmann/json.hpp`）: MIT。ODPT の列車ロケーション（JSON）を C++ 側で読むために入れてある
