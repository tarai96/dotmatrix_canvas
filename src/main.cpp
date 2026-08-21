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
const char* mqtt_topic  = "esp32/ledpad/draw/anatasikamienaino_12345";

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_8x8matrix matrix = Adafruit_8x8matrix();

// 点灯時刻記録（1秒後に自動消灯）
unsigned long dotOnTime[8][8] = {0};
const unsigned long DISPLAY_DURATION = 1000; 

// MQTTメッセージ受信用コールバック関数
void callback(char* topic, byte* payload, unsigned int length) {
    // 届いたデータ（例: "3,5"）をパース
    char message[16] = {0};
    for (int i = 0; i < length && i < 15; i++) {
        message[i] = (char)payload[i];
    }

    int x = -1, y = -1;
    if (sscanf(message, "%d,%d", &x, &y) == 2) {
        if (x >= 0 && x < 8 && y >= 0 && y < 8) {
            dotOnTime[x][y] = millis(); // 受信時刻を記録
        }
    }
}

// MQTTサーバーへの再接続処理
void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ESP32Client-" + String(random(0xffff), HEX);
        
        if (client.connect(clientId.c_str())) {
            Serial.println("connected");
            client.subscribe(mqtt_topic); // トピックの購読開始
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 2 seconds");
            delay(2000);
        }
    }
}

// 表示更新（上下反転補正付き）
void updateDisplay() {
    matrix.clear();
    unsigned long now = millis();

    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            if (dotOnTime[x][y] > 0 && (now - dotOnTime[x][y] < DISPLAY_DURATION)) {
                matrix.drawPixel(x, y, LED_ON);
            } else if (dotOnTime[x][y] > 0) {
                dotOnTime[x][y] = 0;
            }
        }
    }

    // 上下反転補正処理
    for (int x = 0; x < 8; x++) {
        uint8_t originalLine = matrix.displaybuffer[x];
        uint8_t reversedLine = 0;
        for (int b = 0; b < 8; b++) {
            if (originalLine & (1 << b)) {
                reversedLine |= (1 << (7 - b));
            }
        }
        matrix.displaybuffer[x] = reversedLine;
    }

    matrix.writeDisplay();
}

void setup() {
    Serial.begin(115200);

    matrix.begin(0x70);
    matrix.setBrightness(2);
    matrix.clear();
    matrix.writeDisplay();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");

    // MQTTサーバー設定
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnectMQTT();
    }
    client.loop(); // MQTT通信の維持・受信チェック

    updateDisplay(); // 1秒経過チェック & 描画更新
    delay(20);
}