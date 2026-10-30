#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

// Wi-Fi設定
#include "secrets.h"
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// MQTTブローカー設定（パブリックブローカーを利用）
const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

// ★ユニークなトピック名を指定（他者と衝突しないよう適宜変更してください）
const char* mqtt_topic  = "esp32/ledpad/draw/anatasikamienaino";

WiFiClient espClient;
PubSubClient client(espClient);

// 8x16 ドットマトリクス用オブジェクト
Adafruit_8x16matrix top = Adafruit_8x16matrix();
Adafruit_8x16matrix bottom = Adafruit_8x16matrix();

// 点灯時刻記録用配列 (横16 × 縦16)
unsigned long dotOnTime[16][16] = {0};
const unsigned long DISPLAY_DURATION = 1000; // 1秒間点灯

// MQTTメッセージ受信用コールバック関数
void callback(char* topic, byte* payload, unsigned int length) {
    char message[16] = {0};
    for (int i = 0; i < length && i < 15; i++) {
        message[i] = (char)payload[i];
    }

    int x = -1, y = -1;
    if (sscanf(message, "%d,%d", &x, &y) == 2) {
        // x座標の範囲チェックを 0〜15 に拡張
        if (x >= 0 && x < 16 && y >= 0 && y < 16) {
            dotOnTime[x][y] = millis();
            Serial.print("("); Serial.print(x);
            Serial.print(","); Serial.print(y);
            Serial.println(")");
        }
    }
}

void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ESP32Client-" + String(random(0xffff), HEX);
        
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
            client.subscribe(mqtt_topic);
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 2 seconds");
            delay(2000);
        }
    }
}

// 画面描画処理（8x16対応 ＆ 確実な上下反転補正）
void updateDisplay() {
    top.clear();
    bottom.clear();
    unsigned long now = millis();

    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            if (dotOnTime[x][y] > 0 && (now - dotOnTime[x][y] < DISPLAY_DURATION)) {
                // X座標を「15 - x」にして左右を反転させて描画します
                // （上下は元の「7 - y」をやめて y のままにすることで反転）
                if(y < 8){
                    top.drawPixel(15-x, y, LED_ON);
                }else{
                    bottom.drawPixel(15-x, y-8, LED_ON);
                }
            } else if (dotOnTime[x][y] > 0) {
                dotOnTime[x][y] = 0;
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
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop();

    updateDisplay();
    delay(20);
}