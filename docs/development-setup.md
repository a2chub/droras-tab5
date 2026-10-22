# 開発環境セットアップ（M5Stack Tab5）

基盤構成と従来の手順は 2026-10-06 時点で実機確認済み。端末上の Wi-Fi 編集機能は 2026-10-22 に実機へ書き込み、
起動・入力画面・入力チェック・キャンセルを確認済み。接続成功・保存後の設定保持は未確認。アプリの仕様は [heatboard.md](heatboard.md) を参照。
追加したヘッダーは実機で未設定・Wi-Fi接続待ちの表示を確認済み。電波強度の実測とサーバー通信断・復旧は未検証。

## 構成

| 項目 | 内容 |
|---|---|
| デバイス | M5Stack Tab5（ESP32-P4 **rev v1.3** / Flash 16MB / PSRAM 32MB / 1280x720） |
| 無線 | ESP32-C6（SDIO 接続、**2.4GHz のみ**。5GHz には接続できない） |
| 接続 | USB-C = ESP32-P4 内蔵 USB-Serial/JTAG（macOS では `/dev/cu.usbmodemXXXX`） |
| ビルド | PlatformIO Core 6.1.19 + pioarduino platform-espressif32 55.03.38-1 |
| フレームワーク | ESP-IDF 5.5.4（PlatformIO 管理。ネイティブの `idf.py` は未導入） |
| ライブラリ | M5Unified / M5GFX、esp_hosted 2.x + esp_wifi_remote、esp_websocket_client、FreeType、esp_video + esp_cam_sensor（カメラ）、ESP-DL + hand_detect + hand_gesture_recognition（ESP-IDF コンポーネントマネージャ経由、`dependencies.lock` で固定） |
| フォント | IPAex ゴシック（`assets/fonts/ipaexg.ttf`、ファームに埋め込み。ライセンスは同フォルダ） |

Arduino コアは導入していない（追加で数GB必要なため）。

## コマンド

`pio` は PATH に入っていないため、フルパスで実行する。

```sh
PIO=~/.platformio/penv/bin/pio

$PIO run -e tab5              # ビルド
$PIO run -e tab5 -t upload    # ビルドして書き込み（フォント込みで約8MB、約1分）
$PIO device monitor           # シリアルログ（終了は Ctrl+C）
$PIO test -e native           # components/heatboard_core の単体テスト（PC上で実行）
$PIO test -e native_wifi_station # Wi-Fiの反映・再接続テスト（ESP-IDF/NVSを模擬）
```

日本語を含む作業フォルダーでは、PlatformIO のリンカー設定生成時にパスが欠落し、
`cannot find /.pio/build/tab5/firmware.map` となる場合がある。macOS では英数字の実パスを
ビルド先にすると回避できる（`/tmp` は `/private/tmp` へのリンクなので後者を指定する）。

```sh
TAB5_BUILD_DIR="$(mktemp -d /private/tmp/droras-tab5-build.XXXXXX)"
PLATFORMIO_BUILD_DIR="$TAB5_BUILD_DIR" "$PIO" run -e tab5
# 書き込み時も同じ PLATFORMIO_BUILD_DIR を指定して -t upload を追加する。
```

### 画面キャプチャとリモート操作

USB シリアル経由で画面を PNG に保存し、タッチやキー操作を送れる（`src/debug_console.h`）。

```sh
PY=~/.platformio/penv/bin/python
$PY tools/screenshot.py out.png                       # 現在の画面を保存（約2秒）
$PY tools/screenshot.py out.png --send "tap 1190 28"  # 座標をタップしてから保存
$PY tools/screenshot.py out.png --send "key 3"        # key 1=スタート/ストップ 2=前 3=次
$PY tools/screenshot.py out.png --send "imu"          # 加速度と向きの判定をシリアルログに出す
$PY tools/screenshot.py cam.png --command camshot     # ジェスチャー認識が見ている画像（320x180）を保存
$PY tools/screenshot.py out.png --send "camtest"      # 内蔵サンプル画像でモデルを動かす（ログに "one" が出れば正常）
$PY tools/screenshot.py out.png --send "gest 2 3000"  # 3秒間「ピース」を見たことにする（発火と表示の確認）
$PY tools/screenshot.py out.png --send "cam 1000 50"  # 露出 1000 ライン・ゲイン index 50 に固定（"cam auto" で自動に戻す）
```

キャプチャとタップの座標は、上下反転表示中も画面に見えている向き（UI の座標）のまま。

## Wi-Fi の設定

`src/app_secrets.h` が無くてもビルドできる。書き込み後は本体だけで設定する。

1. 「設定 → Wi-Fi」で、4枠のうち「未登録・タップして設定」または「編集」を押す。
2. SSID／パスワードの入力欄をタップし、英数字・記号キーボードで入力する。パスワードは既定で伏せ字になり、
   「表示／隠す」で切り替えられる。
3. 「保存して接続」を押す。接続中の枠を編集した場合も、新しい内容で再接続する。
   接続できなければ同じ枠を再編集できる。「キャンセル」では入力内容を保存しない。

SSID は空欄不可、最大32バイト。前後の空白を保持し、制御文字は受け付けない。
パスワードは空欄（暗号化なし）、8〜63文字のASCII英数字・記号・空白、または64桁の16進PSK。
キーボードでは日本語の SSID を新規入力できない。周囲のアクセスポイントを検索する機能はない。

従来の `src/app_secrets.h`（git 管理外）も任意の初期値として利用できる。
必要な場合だけ `src/app_secrets.example.h` をコピーして、最大4件のラベル・SSID・パスワードを記入する。
各枠では本体に保存した値を優先し、保存値のない枠はソース側の初期値を使う。

SSID とパスワードは枠ごとに1つの NVS blob に保存し、選択中の枠には既存の `wifi_slot` キーを使う。
再起動後も保存内容を読み込む。保存エラー時は入力内容を残し、成功するまで接続設定を切り替えない。
既存の NVS パーティション構成と `tools/restore_release.sh` は変更していない。
2026-10-22、接続された ESP32-P4 rev1.3 に書き込み済み。書き込み前の16MB全域を退避し、
esptool の照合で本体との一致を確認した。個別の bootloader・partition・app を書き込み、
書き込み直後の NVS 24KB が書き込み前と一致すること、起動ログに NVS 消去・クラッシュがないことを確認した。

### Wi-Fi 編集機能の実機確認項目

チェック済みの項目は USB 経由のタップ送信で実機の画面と動作を確認した。
テスト入力は保存せずキャンセルした。2026-10-22のUI確認後はボード画面へ戻した。
既存の端末を使う前に、下記の「リリースの保存と復元」の手順で現在のファームと設定を退避する。
空設定や保存エラーの試験は、復元可能な試験端末・試験環境で行う。

- [x] `app_secrets.h` なし・保存済み Wi-Fi なしで起動し、設定画面から編集画面を開ける。
- [ ] 正しい SSID／パスワードを入力すると接続し、IP アドレスが表示される。
- [x] 英小文字・大文字・記号・空白、削除と入力欄の切り替えが意図どおりに動く。
- [x] パスワードの伏せ字と「表示／隠す」が切り替わる。
- [ ] 間違ったパスワードで失敗した後、同じ枠を修正して接続できる。
- [ ] 接続中の枠を編集して保存すると、新しい設定で再接続する。
- [ ] 複数の枠に登録し、接続先を切り替えられる。
- [ ] 再起動後も SSID／パスワードと選択した枠が保持される。
- [x] 編集をキャンセルすると保存内容と接続先が変わらない。
- [x] SSID の空欄と、短すぎるパスワードが拒否される。
- [x] SSID の33バイト以上の入力が拒否される。
- [x] パスワード64文字の表示と65文字目の拒否、64文字から63文字への削除を確認する。
- [ ] 対応する試験用アクセスポイントで、暗号化なしと64桁の16進PSKを確認する。
- [ ] 保存エラーを注入した場合、成功表示や接続切り替えを行わず、入力を残して再保存できる。
- [ ] 既存のソース側初期値を読み込み、同じ枠を本体で保存した後は保存値が優先される。
- [ ] 保存済みの旧版ファームへ戻せることと、復元後の接続先を確認する。

### 電波強度・サーバー生存確認の実機確認項目

電波強度は約2秒周期の RSSI 取得、サーバーは既存 Socket.IO 接続の Engine.IO heartbeat で確認する。
サーバーの応答期限は通知された `pingInterval + pingTimeout`（最大120秒）、接続確立待ちは各段階10秒。
失敗後は3秒待って再試行する。詳細な表示仕様は [heatboard.md](heatboard.md#wi-fi-電波強度とサーバーの生存確認) を参照。

- [ ] Wi-Fi 接続時にヘッダーのバーと設定画面の RSSI が表示され、電波強度の変化が反映される。
- [ ] RSSI 取得失敗時は不明表示になり、切断時は古い測定値を表示し続けない。
- [ ] サーバー未設定／初回接続確認中／応答ありの状態と色を区別できる。
- [ ] droras の停止や heartbeat の停止で赤い応答停止表示になり、復旧後に自動で正常へ戻る。
- [ ] Wi-Fi 切断後にサーバーの正常表示が残らず、Wi-Fi 復旧後に再接続する。
- [x] Wi-Fi未設定時、保存済みサーバーはWi-Fi接続待ちと表示される。
- [x] 通常の向きで、ボード画面・設定全6タブ・Wi-Fi編集中のアイコンや入力内容が重ならない。
- [ ] Wi-Fi編集中の接続状態変化と、上下反転時の表示・タッチ位置を実機で確認する。

### USB操作によるUI確認（2026-10-22）

USBの `tap` コマンドで実機の画面遷移・入力操作を行い、1280×720のPNGを34枚取得した。
ボード、設定全6タブ、Wi-Fi編集、エラー表示、入力上限、キャンセルを確認した。
カメラの操作無効時説明と長いSSIDボタンの文字サイズを修正し、パスワードの説明に64桁の16進数を追記した。
カメラ説明は修正後の実画像で全文表示を確認し、32文字の長いSSIDボタンは同梱フォントの幅計測で確認した。
試験用SSID・パスワードは保存していない。ジェスチャー設定は試験時のみ無効にし、元の有効設定へ戻した。

連続したUSBタップでは `tools/screenshot.py --settle 0.4` を使う。0.12秒間隔の初回試験では64回中57文字しか
反映されなかったため、0.4秒間隔で再試験して64文字と65文字目の拒否を確認した。
USBコマンドキューは8件で満杯時の投入失敗を報告しないため、高速な連打は画面上の文字数でも確認する。
物理タッチパネルの押下・解放と通信のE2Eは、このUSB試験とは別に確認する。

## はまりどころ

実際に踏んだもの。設定ファイル側にも理由をコメントしてある。

- **チップリビジョン**: ESP-IDF 5.5 は既定で ESP32-P4 rev 3.0 以上向けにビルドする。この個体は rev 1.3 なので
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` が必須（`sdkconfig.defaults`）。
- **パーティション表**: PlatformIO は `sdkconfig` の `CONFIG_PARTITION_TABLE_*` を無視し、
  `board_build.partitions`（未指定ならアプリ領域 1MB の既定表）を書き込む。アプリが領域を超えると
  `Image length ... doesn't fit in partition` で起動ループになる。`partitions.csv` を変更するときは
  `platformio.ini` と `sdkconfig.defaults` の両方が同じファイルを指していることを確認する。
- **`sdkconfig.defaults` の変更が反映されない**: 生成済みの `sdkconfig.tab5` を削除してから再ビルドする。
- **git**: コミットが1つも無い状態の git リポジトリ（`git init` 直後）ではビルドの CMake 構成が失敗する。
  `git init` したら最初のコミットまで済ませる。
- **書き込みポート**: `upload_port` にワイルドカード（`/dev/cu.usbmodem*`）は使えない。自動検出に任せている。
- **ログ出力先**: USB-C は UART0 ではないため `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` が必要。
- **シリアルポートを開くとリセットされる**: DTR/RTS を明示的に下げて開くと、USB-Serial/JTAG がそれを
  リセット要求と解釈して再起動する。pyserial の既定（両方アサート）のまま開く。
- **esp_hosted は 2.x に固定**: 3.x は `-include <ファイル>` という2語のコンパイルオプションを使い、
  PlatformIO のビルダーがこれを分断して全ファイルのコンパイルが失敗する。
- **esp_hosted の初期化**: 本来は静的コンストラクタで自動初期化されるが、PlatformIO のリンクでは
  そのオブジェクトが取り込まれない。`esp_wifi_init()` の前に `esp_hosted_init()` を明示的に呼ぶ（`wifi_station.cpp`）。
- **C6 のリセット極性**: esp_hosted の Tab5 プリセットは「active low」を既定にするが、この個体では
  C6 がリセットされたままになり SDIO が応答しない（`send_op_cond returned 0x107`）。
  `CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y` で動作する。
- **C6 側ファームが古い**: 起動時に `Version mismatch: Host [2.12.0] > Co-proc [0.0.0]` の警告が出る。
  現状の機能（STA 接続、TCP/TLS）は問題なく動いているが、RPC タイムアウトが出るようなら C6 側の更新を検討する。
- **内蔵ビットマップフォントは使わない**: M5GFX の日本語フォントは JIS 第1水準＋αまでで、
  邉・螻・髙 などが表示できない（最大 40px）。そのため FreeType + IPAex ゴシックで描画している。
- **横向きのまま大きな画像を転送しない**: パネルは物理的に縦長（720x1280）で、M5GFX は横向きへの書き込みを
  1ピクセルずつ回転する（約0.7µs/px。ヒート表の転送だけで約450ms）。行単位の memcpy になるのは
  「回転なし・同じ色形式」のときだけなので、`canvas.cpp` はパネルと同じメモリ配置・同じ形式
  （リトルエンディアン RGB565）の裏バッファに描き、転送の間だけ表示を本来の向きに切り替えている。
- **`pushAlphaImage` は使わない**: 横向きに回転した状態では、潰れた絵が違う位置に描かれる。
  文字の合成は裏バッファのメモリに直接行っている。
- **PSRAM の帯域が描画の下限**: フレームバッファも裏バッファも PSRAM 上にあり、表の領域（約1MB）を
  塗るのに約45ms、パネルへ転送するのに約50ms かかる。文字のラスタライズはグリフキャッシュで、
  塗りは隣のヒートの表を事前に描いておくこと（`board_screen.cpp` の PreparedTable）で省いているが、
  転送の約50ms は減らせない。
- **加速度センサーの軸**: M5Unified が返す Tab5 の加速度は、横向きを通常の向きで立てると X ≈ -1g、
  上下逆さで X ≈ +1g、平置きで Z ≈ -1g（実測）。`screen_orientation.cpp` の `downAxisG()` がこれに依存する。
- **カメラの I2C は M5GFX と共有**: センサー（SC2356、SC202CS ドライバ）の SCCB は内部 I2C（I2C_NUM_1、SDA=31/SCL=32）で、
  タッチ・IMU・IO エキスパンダと同じバス。M5GFX は「先に `i2c_master` のバスが作られていればそれを共有する」ので、
  `M5.begin()` より前に `i2c_new_master_bus()` し、そのハンドルを esp_video に渡している（`hand_gesture.cpp`）。
  共有は排他制御されないため、センサーへの I2C アクセス（esp_video の初期化・露出設定）は必ずメインタスクから行う。
  このため ISP パイプラインコントローラ（esp_ipa、別タスクで I2C を叩く）は使わず、露出・ゲイン・WB は自前。
  なお esp_ipa の既製ライブラリは rev 1.3 で不正命令になるという報告もある（esp-video-components #99）。
- **ESP-DL は rev 1.3 で動く**: 公式サンプル画像で手を検出・分類できることを確認済み（`camtest`）。
- **検出器は大きな手を見つけられない**: 公式画像の手を画面の 6 割以上に拡大すると検出されなくなる。
  720px 切り出しでは腕を伸ばした手が大きすぎて全く検出できなかったので、解析画像はフレーム全体の 1/4 縮小にした。
- **PPA の縮小率は 1/16 刻み**: 1/3 を指定すると 5/16 になり、出力バッファの端に未描画の縞が残る。1/2、1/4 など正確に表せる値を使う。
- **カメラは上下逆に付いている**: 横向き UI に対して映像が 180° 回転しているので、PPA で回転して解析している
  （本体を上下反転表示にしているときは回転なし）。
- **分類器には検出枠をそのまま渡す**: 指が枠から外れる対策に枠を 50% 広げたところ、分類器が全て no_hand を返すようになった
  （学習時の切り出し方と違うため）。大きすぎる手は距離を取ってもらう。
- **ノイズと白飛びが分類を壊す**: 暗所ではゲインが上限に張り付いて画像がざらつき、検出されない。PPA で 1/2 にしてから 2x2 平均で
  1/4 にし、ISP の BF を強めにして改善。逆に近くの手は背景より明るく飽和して肌に見えなくなるため、露出制御は
  「中央の明るい 5% が 235 を超えたら下げる」を優先する。
- **ホワイトバランスは全体で測る**: 手だけが写る切り出し領域でグレーワールドを計算すると肌色が灰色に補正され、認識できなくなる。
- **シリアルへの長い行は直接ドライバに書く**: stdio 経由だと長い行が分割され、他タスクのログが行の途中に混ざって
  画面ダンプのデコードに失敗する。`usb_serial_jtag_write_bytes()` で 1 行を 1 回で書く。
- **メインタスクのスタック**: FreeType のラスタライザがスタック上に 16KB のバッファを取るため 32KB にしている。

## リリースの保存と復元（いつでも前の版に戻す）

安定した版は 3 段構えで残してあり、どれからでも再ビルドなしで戻せる。

| 何を | どこに | 戻せるもの |
|---|---|---|
| ソース | git タグ `vX.Y.Z`（`origin` にも push 済み） | ソースコード。再ビルドして書き込む |
| ビルド済みファーム | `firmware_backup/vX.Y.Z/`（bootloader / partitions / firmware の各 .bin と .elf、`SHA256SUMS`） | ファームだけ。本体の設定（NVS）はそのまま残る |
| Flash 全域のダンプ | `firmware_backup/tab5_vX.Y.Z_full_16MB.bin` | ファームと設定を含む、保存した時点の完全な状態 |

`firmware_backup/` は Wi-Fi のパスワードを含む NVS が入るため git 管理外。**このフォルダは別途バックアップすること。**

```sh
tools/restore_release.sh v1.0.0          # ファームだけ戻す（設定は残る）。約30秒
tools/restore_release.sh v1.0.0 --full   # 設定ごと保存時点の状態に戻す。約4分
```

書き込み後は本体が再起動する。ソースから戻すなら `git switch --detach v1.0.0 && $PIO run -e tab5 -t upload`。

### 新しい版を保存する手順

1. 実機で動作を確認し、main にマージしてから `git tag -a vX.Y.Z -m "..." && git push origin vX.Y.Z`
2. ビルド成果物を保存:
   ```sh
   mkdir firmware_backup/vX.Y.Z
   cp .pio/build/tab5/{bootloader,partitions,firmware}.bin .pio/build/tab5/firmware.elf firmware_backup/vX.Y.Z/
   (cd firmware_backup/vX.Y.Z && shasum -a 256 *.bin > SHA256SUMS)
   ```
3. Flash 全域を退避: `uvx esptool --port /dev/cu.usbmodemXXXX --baud 921600 read-flash 0 0x1000000 firmware_backup/tab5_vX.Y.Z_full_16MB.bin`
4. ここ（下の一覧）に SHA-256 を記録する

### 保存済みの版

| 版 | 内容 | コミット | 全域ダンプの SHA-256 |
|---|---|---|---|
| v1.0.0 | ヒート表、設定（Wi-Fi / サーバー / 音量・明るさ / 画面の向き）。カメラ対応前 | `ed9377e` | `cc02e866f5436f318e88e2a015a346604696538fe8f2ad4107b400dba85b7c9b` |

## 工場出荷ファームウェアの復元

書き込み前に Flash 全域（16MB）を `firmware_backup/tab5_factory_16MB.bin` に退避してある
（git 管理外。SHA-256: `a260eda93e4e5ddc1d3abddd361b1eb326c07d4248b67d1b493feba057fde231`）。
2台目（MAC `80:f1:b2:d1:40:d3`、2026-10-11 入手）の新品ダンプも同じ SHA-256 だったため、
工場出荷イメージに個体固有のデータは含まれず、どの個体にもこのファイルで戻せる。

```sh
uvx esptool --port /dev/cu.usbmodemXXXX write-flash 0 firmware_backup/tab5_factory_16MB.bin
```

M5Burner（インストール済み）から公式ファームウェアを書き込むことでも戻せる。
