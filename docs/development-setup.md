# 開発環境セットアップ（M5Stack Tab5）

2026-10-06 時点で実機確認済みの構成と手順。アプリの仕様は [heatboard.md](heatboard.md) を参照。

## 構成

| 項目 | 内容 |
|---|---|
| デバイス | M5Stack Tab5（ESP32-P4 **rev v1.3** / Flash 16MB / PSRAM 32MB / 1280x720） |
| 無線 | ESP32-C6（SDIO 接続、**2.4GHz のみ**。5GHz には接続できない） |
| 接続 | USB-C = ESP32-P4 内蔵 USB-Serial/JTAG（macOS では `/dev/cu.usbmodemXXXX`） |
| ビルド | PlatformIO Core 6.1.19 + pioarduino platform-espressif32 55.03.38-1 |
| フレームワーク | ESP-IDF 5.5.4（PlatformIO 管理。ネイティブの `idf.py` は未導入） |
| ライブラリ | M5Unified / M5GFX、esp_hosted 2.x + esp_wifi_remote、esp_websocket_client、FreeType（ESP-IDF コンポーネントマネージャ経由、`dependencies.lock` で固定） |
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
```

### 画面キャプチャとリモート操作

USB シリアル経由で画面を PNG に保存し、タッチやキー操作を送れる（`src/debug_console.h`）。

```sh
PY=~/.platformio/penv/bin/python
$PY tools/screenshot.py out.png                       # 現在の画面を保存（約2秒）
$PY tools/screenshot.py out.png --send "tap 1190 28"  # 座標をタップしてから保存
$PY tools/screenshot.py out.png --send "key 3"        # key 1=スタート/ストップ 2=前 3=次
$PY tools/screenshot.py out.png --send "imu"          # 加速度と向きの判定をシリアルログに出す
```

キャプチャとタップの座標は、上下反転表示中も画面に見えている向き（UI の座標）のまま。

## Wi-Fi の設定

`src/app_secrets.example.h` を `src/app_secrets.h`（git 管理外）にコピーし、最大4件の
ラベル・SSID・パスワードを記入して書き込む。どれを使うかは実機の「設定 → Wi-Fi」で選ぶ。

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

```sh
uvx esptool --port /dev/cu.usbmodemXXXX write-flash 0 firmware_backup/tab5_factory_16MB.bin
```

M5Burner（インストール済み）から公式ファームウェアを書き込むことでも戻せる。
