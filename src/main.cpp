#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>  // MQTTPubSubClient.h より先に include する
#include <MQTTPubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

// Wi-Fi設定
#include "secrets.h"
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// MQTTブローカー設定（パブリックブローカーを利用）
// 学校Wi-Fiでは 1883 番がふさがれているので、ブラウザと同じ wss(8884) で接続する
const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 8884;
const char* mqtt_path   = "/mqtt";

// ★ユニークなトピック名を指定（他者と衝突しないよう適宜変更してください）
const char* mqtt_topic  = "esp32/ledpad/draw/anatasikamienaino";

WebSocketsClient wsClient;
MQTTPubSubClient mqtt;

// 8x16 ドットマトリクス用オブジェクト
Adafruit_8x16matrix top = Adafruit_8x16matrix();
Adafruit_8x16matrix bottom = Adafruit_8x16matrix();

// 点灯時刻記録用配列 (横16 × 縦16)
unsigned long dotOnTime[16][16] = {0};
const unsigned long DISPLAY_DURATION = 1000; // 1秒間点灯

// お絵描きモードで描いた点（消すまで点灯し続ける）
bool drawn[16][16] = {false};

bool inRange(int x, int y) {
    return x >= 0 && x < 16 && y >= 0 && y < 16;
}

// MQTTメッセージ受信用コールバック関数
// 受け取るメッセージ:
//   "x,y"   軌跡モード（1秒で消える）
//   "d,x,y" お絵描きモード（消すまで残る）
//   "e,x,y" 消しゴム
//   "c"     全部消す
void callback(const String& payload, const size_t size) {
    char message[16] = {0};
    strncpy(message, payload.c_str(), sizeof(message) - 1);

    int x = -1, y = -1;
    if (strcmp(message, "c") == 0) {
        memset(drawn, 0, sizeof(drawn));
        memset(dotOnTime, 0, sizeof(dotOnTime));
        Serial.println("clear");
    } else if (sscanf(message, "d,%d,%d", &x, &y) == 2) {
        if (inRange(x, y)) drawn[x][y] = true;
    } else if (sscanf(message, "e,%d,%d", &x, &y) == 2) {
        if (inRange(x, y)) drawn[x][y] = false;
    } else if (sscanf(message, "%d,%d", &x, &y) == 2) {
        if (inRange(x, y)) {
            dotOnTime[x][y] = millis();
            Serial.print("("); Serial.print(x);
            Serial.print(","); Serial.print(y);
            Serial.println(")");
        }
    }
}

void reconnectMQTT() {
connect_to_host:
    // WebSocket(wss)でブローカーに接続（"mqtt" はサブプロトコル名で必須）
    Serial.println("Connecting to WebSocket...");
    wsClient.disconnect();
    wsClient.beginSSL(mqtt_server, mqtt_port, mqtt_path, "", "mqtt");
    wsClient.setReconnectInterval(2000);

    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    while (!mqtt.connect(clientId)) {
        Serial.print(".");
        delay(1000);
        if (!wsClient.isConnected()) {
            Serial.println(" WebSocket disconnected, retrying");
            goto connect_to_host;
        }
    }
    Serial.println(" connected");

    // 再接続のたびに購読し直す（クリーンセッションなので切断で購読が消える）
    mqtt.subscribe(mqtt_topic, callback);
}

// 画面描画処理（8x16対応 ＆ 確実な上下反転補正）
void updateDisplay() {
    top.clear();
    bottom.clear();
    unsigned long now = millis();

    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            bool trailOn = dotOnTime[x][y] > 0 && (now - dotOnTime[x][y] < DISPLAY_DURATION);
            if (!trailOn && dotOnTime[x][y] > 0) {
                dotOnTime[x][y] = 0;
            }

            // 軌跡の点か、お絵描きで描いた点なら点灯
            if (trailOn || drawn[x][y]) {
                // X座標を「15 - x」にして左右を反転させて描画します
                // （上下は元の「7 - y」をやめて y のままにすることで反転）
                if(y < 8){
                    top.drawPixel(15-x, y, LED_ON);
                }else{
                    bottom.drawPixel(15-x, y-8, LED_ON);
                }
            }
        }
    }

    // ディスプレイバッファへの直接操作（ループ処理）は不要になります
    top.writeDisplay();
    bottom.writeDisplay();
}

void setup() {
    Serial.begin(115200);

    // 8x16ドットマトリクス初期化
    top.begin(0x70);
    top.setRotation(1);
    top.setBrightness(2);
    top.clear();
    top.writeDisplay();
    bottom.begin(0x71);
    bottom.setRotation(1);
    bottom.setBrightness(2);
    bottom.clear();
    bottom.writeDisplay();

    WiFi.begin(ssid, password);

    /*
    // 学校Wi-Fi（WPA2-Enterprise / PEAP）
    WiFi.begin("TKU-NET-WiFi2", WPA2_AUTH_PEAP,
           TKU_ID,        // 匿名ID
           TKU_ID,        // ID
           TKU_PASSWORD); // パスワード
    */
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");

    mqtt.begin(wsClient);
}

void loop() {
    mqtt.update(); // MQTT通信の維持・受信チェック
    if (!mqtt.isConnected()) {
        reconnectMQTT();
    }

    updateDisplay();
    delay(20);
}