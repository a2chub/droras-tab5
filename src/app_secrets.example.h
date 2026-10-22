// Wi-Fi の任意の初期値。app_secrets.h がなくてもビルドでき、設定は端末だけで行えます。
// ソース側の初期値が必要な場合だけ、このファイルを app_secrets.h（git 管理外）にコピーしてください。
// 最大4枠。本体の「設定 → Wi-Fi」で保存した各枠の値が、ここで指定した値より優先されます。
//   LABEL    設定画面のボタン名（例: "会場", "自宅"）。空なら SSID を表示
//   SSID     最大32バイト。未使用の枠は空欄（""）。日本語も指定可能ですが、端末のキーボードでは新規入力できません
//   PASSWORD ASCII英数字・記号・空白の8〜63文字、64桁の16進PSK、または空欄（暗号化なし）
// Tab5 の無線（ESP32-C6）は2.4GHzのみ対応しています。
#pragma once

#define APP_WIFI_1_LABEL ""
#define APP_WIFI_1_SSID ""
#define APP_WIFI_1_PASSWORD ""

#define APP_WIFI_2_LABEL ""
#define APP_WIFI_2_SSID ""
#define APP_WIFI_2_PASSWORD ""

#define APP_WIFI_3_LABEL ""
#define APP_WIFI_3_SSID ""
#define APP_WIFI_3_PASSWORD ""

#define APP_WIFI_4_LABEL ""
#define APP_WIFI_4_SSID ""
#define APP_WIFI_4_PASSWORD ""
