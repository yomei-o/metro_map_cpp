# metro_map_cpp — 東京の地下鉄の列車を、光る地図に走らせる（C++ / WASM）

東京メトロ 9 路線と都営地下鉄 4 路線の線路・駅を、暗い背景に光る線で描きます。列車は流れ星のように尾を引いて走ります。
地図は「実際の地図」と「路線図」の 2 種類で、ボタンを押すと駅がなめらかに動いて変形します。

**デモ**: https://yomei-o.github.io/metro_map_cpp/
（ドラッグで移動、ホイールまたは 2 本指のピンチで拡大縮小、駅をタップ・マウスを乗せると駅名と路線。スマホでも同じ大きさの文字になるよう、画面の倍率に合わせて文字と線を太くしています）

描画はすべて C++（線・光・円・日本語の文字まで自前のソフトウェア描画）です。ブラウザでは WebAssembly にして、できた RGBA を canvas に貼るだけです。
同じ `metro_map.hpp` をネイティブでビルドすると、同じ絵を PNG に書き出せます（`metro_map.exe`）。

**都営 4 路線（浅草・三田・新宿・大江戸）の列車は、いま実際に走っている列車です。** 公共交通オープンデータセンター（ODPT）の列車ロケーション情報（`odpt:Train`）を、ブラウザから約 30 秒ごとに取っています。
都営のデータはキー無しの公開用 API（`https://api-public.odpt.org/api/v4/`、`Access-Control-Allow-Origin: *`）で取れるので、公開ページにキーを置かずに済みます。

**東京メトロ 9 路線の列車は、時刻表（ダイヤ）どおりの位置です。** 東京メトロは列車の位置（`odpt:Train`）を公開していません（ODPT で位置を出しているのは都営と横浜市営だけ）。
そこでキーの要る時刻表（`odpt:TrainTimetable`）をビルド時に取って `data/odpt_metro.txt` に焼き、ブラウザは「いまこの時刻にどの列車がどの駅間にいるはず」をダイヤから計算して走らせます。
ページにキーは埋めず、メトロは通信ゼロです（時刻表は焼き込み済み）。「実際の位置」ではなく「ダイヤ上の位置」なので、遅延は反映しません。発車時刻の瞬間は正しい駅にいて、駅間は補間です。
なお ODPT のメトロ時刻表は各駅の発車時刻だけで到着時刻が無いため、通過待ち（待避）の「待っている時間」は再現できません。

### 都営の列車の動かし方

ODPT が教えてくれるのは「どの駅にいるか（`toStation` が null）」「どの駅とどの駅のあいだにいるか」だけで、更新は約 30 秒ごとです。
あいだの位置は、時刻表（`odpt:TrainTimetable`）から出した区間の平均の所要時間（`data/odpt_toei.txt` の `run`）で進めます。

- あいだにいると言われた → 所要時間で進める。着いたと言われるまでは 97% の手前で待つ
- 着いたと言われた → 残りを数秒で進めて駅に止める
- 前の駅から次の駅へ、あいだを見ないまま移っていた（更新の 30 秒のうちに走り終えた） → 遅れを取り戻すよう早めに走らせる
- 尾は、止まっているあいだも来た向きへ出す。向きは `odpt:railDirection` と駅の並び（`odpt:stationOrder`、`data/odpt_toei.txt` の `order`）から「1 つ後ろの駅」を出して決める（走っている列車 185 件で、向きと並びは全部一致した）
- 直通先（京急・京成・北総・東急・相鉄など）を走っている列車は、地図に無いので出さない（2026-11-11 夕方で約 120 本中 95 本ほどが地図に載る）
- 駅名は ODPT の `odpt:Railway` の駅の並びの日本語名で、地図（国土数値情報）の駅名と 4 路線とも全部一致した。直通先の行き先 29 駅の名前は `tools/build_odpt_toei.py` に表で持つ（他社の駅のデータはキー無しでは取れない）

## しくみ

| ファイル | やること |
|---|---|
| `tools/build_network.py` | 国土数値情報（鉄道データ N02-24）から、地下鉄 13 路線の線路と駅を `data/network.txt` に取り出す |
| `tools/build_admin.py` | 国土数値情報（行政区域データ N03）から、東京都の外周（市区町村を dissolve した本土の外周）と 23 区の区界を `data/admin_tokyo.txt` に取り出す |
| `network.hpp` | 経緯度をメルカトル図法に投影し、同じ名前の近い駅を 1 つの乗換駅にまとめる。路線の線路全体を 20 m おきの点の網にして、各駅から網に沿って隣の駅を探し、駅と駅のあいだ（区間）とその線路の形を作る |
| `layout.hpp` | 路線図の配置を計算する。実際の位置に魚眼をかけて都心を広げた位置を出発点に、「区間の向きを 0°/45°/90° に寄せる」「駅の間隔をそろえる」「途中駅でまっすぐ通す」「無関係な駅を離す」を少しずつ同時にかける（乱数なし、毎回同じ結果） |
| `raster.hpp` | なめらかな太線（継ぎ目が濃くならない）、円、光（縮めてぼかして足す）、stb_truetype の日本語（暗い縁取りつき） |
| `tools/build_odpt_toei.py` | ODPT の都営の路線・時刻表から、駅の対応と区間の所要時間を `data/odpt_toei.txt` に取り出す |
| `tools/build_odpt_metro.py` | ODPT の東京メトロの時刻表（キー要）を、列車ごとの「駅と発車時刻」の表 `data/odpt_metro.txt` に焼く。`metro_map.hpp` が今の時刻の位置を計算して走らせる |
| `odpt.hpp` | `data/odpt_toei.txt` を読み、`odpt:Train` の JSON（nlohmann/json）を地図の駅・路線に結びつける |
| `metro_map.hpp` | 2 種類の地図の描画と変形、駅名の配置（乗換駅が先、重ならない場所へ）、列車と流れ星の尾、ODPT の列車の動かし方 |
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
# 取っておいた odpt:Train の JSON を 30 秒おきに届いたものとして流して、様子を見る
build/Release/metro_map.exe data/network.txt fonts/NotoSansJP-Bold-subset.ttf out/live 1280 800 data/odpt_toei.txt t1.json t2.json ...
```

データを作り直すとき（国土数値情報を `data/raw/` に置いて）:

```sh
python tools/build_network.py data/raw/n02/UTF-8/N02-24_RailroadSection.geojson data/raw/n02/UTF-8/N02-24_Station.geojson > data/network.txt
python tools/subset_font.py NotoSansJP[wght].ttf data/network.txt fonts/NotoSansJP-Bold-subset.ttf
# ODPT（キー無し）から rw.json と tt_{Asakusa,Mita,Shinjuku,Oedo}.json を <dir> に取ってから（取り方はスクリプトの先頭）
python tools/build_odpt_toei.py <dir> > data/odpt_toei.txt
# 東京メトロのダイヤ（キーが要る。キーはページには出ない）
python tools/build_odpt_metro.py <ODPTのアクセストークン> > data/odpt_metro.txt
```
時刻表が改正されたら `data/odpt_toei.txt`・`data/odpt_metro.txt` を作り直します（メトロは平日・土休の両ダイヤを焼いてあり、ページ側で今日がどちらかを選ぶ。いまのものは 2026-11-12 に取った時刻表から）。

## 出典とライセンス

- コード: BSD-3-Clause（`LICENSE`）
- 線路と駅: 「国土数値情報（鉄道データ）」（国土交通省）N02-24 を加工して作成（CC BY 4.0）。`data/network.txt`、`docs/network.txt`
- 東京都の外周と 23 区の区界（実際の地図のときだけ薄く描く目印）: 「国土数値情報（行政区域データ）」（国土交通省）N03（2025-01-01、東京都）を加工して作成（CC BY 4.0）。`data/admin_tokyo.txt`、`docs/admin_tokyo.txt`
- 列車の位置・時刻表（都営）: 公共交通オープンデータセンター（ODPT）を通じて東京都交通局が提供するデータを使用。`data/odpt_toei.txt` はそれを加工したもの。内容の正確さ・完全さは保証されません
- 時刻表（東京メトロ）: 公共交通オープンデータセンター（ODPT）を通じて東京地下鉄が提供するデータを使用。`data/odpt_metro.txt` はそれを加工したもの（ダイヤ上の位置の計算に使用。遅延は反映しません）。内容の正確さ・完全さは保証されません
- 文字: Noto Sans JP（SIL Open Font License 1.1、`fonts/OFL.txt`）。駅名に使う 350 文字に絞り、太さ 700 に固定したもの
- stb_truetype / stb_image_write: パブリックドメイン（または MIT）
- nlohmann/json 3.12.0（`third_party/nlohmann/json.hpp`）: MIT。ODPT の列車ロケーション（JSON）を C++ 側で読むために入れてある
