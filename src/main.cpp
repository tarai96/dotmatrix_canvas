#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

#include "secrets.h"
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

AsyncWebServer server(80);
Adafruit_8x8matrix matrix = Adafruit_8x8matrix();

// 各ドットが点灯した時刻（millis）を記録する配列（0は消灯状態）
unsigned long dotOnTime[8][8] = {0};

// 消えるまでの時間（ミリ秒）: 1秒 = 1000
const unsigned long DISPLAY_DURATION = 1000; 

// スマホ操作用のHTML/JavaScript
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <meta charset="utf-8">
    <title>LED Trail Pad</title>
    <style>
        body { text-align: center; font-family: sans-serif; background: #121212; color: white; padding: 20px 10px; user-select: none; touch-action: none; }
        h1 { font-size: 20px; color: #00E676; margin-bottom: 5px; }
        p { font-size: 13px; color: #aaa; margin-bottom: 15px; }
        .canvas-container { margin: 0 auto; width: 280px; height: 280px; }
        canvas { background: #222; border: 4px solid #333; border-radius: 12px; touch-action: none; }
    </style>
</head>
<body>
    <h1>LED 軌跡トレーサー</h1>
    <p>なぞった場所が1秒間だけ光ります</p>
    
    <div class="canvas-container">
        <canvas id="pad" width="280" height="280"></canvas>
    </div>

    <script>
        const canvas = document.getElementById('pad');
        const ctx = canvas.getContext('2d');
        const gridSize = 8;
        const cellSize = canvas.width / gridSize;
        let isDrawing = false;
        
        // スマホ側でも各セルの最終点灯時刻を記録（描画アニメーション用）
        let gridTime = Array(8).fill().map(() => Array(8).fill(0));

        function drawGrid() {
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            const now = Date.now();

            for (let x = 0; x < gridSize; x++) {
                for (let y = 0; y < gridSize; y++) {
                    ctx.strokeStyle = '#333';
                    ctx.strokeRect(x * cellSize, y * cellSize, cellSize, cellSize);

                    // 点灯してから1秒未満のセルを描画
                    if (now - gridTime[x][y] < 1000) {
                        ctx.fillStyle = '#00E676';
                        ctx.fillRect(x * cellSize + 2, y * cellSize + 2, cellSize - 4, cellSize - 4);
                    }
                }
            }
            requestAnimationFrame(drawGrid); // スマホ画面上でも残像アニメーションを更新
        }

        function handlePointer(e) {
            const rect = canvas.getBoundingClientRect();
            const clientX = e.touches ? e.touches[0].clientX : e.clientX;
            const clientY = e.touches ? e.touches[0].clientY : e.clientY;

            const x = Math.floor((clientX - rect.left) / cellSize);
            const y = Math.floor((clientY - rect.top) / cellSize);

            if (x >= 0 && x < 8 && y >= 0 && y < 8) {
                const now = Date.now();
                // 100ms以上経っているか、初めて触れたセルならESP32へ送る
                if (now - gridTime[x][y] > 100) {
                    gridTime[x][y] = now;
                    fetch(`/draw?x=${x}&y=${y}`);
                }
            }
        }

        canvas.addEventListener('pointerdown', (e) => { isDrawing = true; handlePointer(e); });
        canvas.addEventListener('pointermove', (e) => { if (isDrawing) handlePointer(e); });
        window.addEventListener('pointerup', () => { isDrawing = false; });

        drawGrid();
    </script>
</body>
</html>
)rawliteral";

// ドットマトリクスの表示更新関数
void updateDisplay() {
    matrix.clear();
    unsigned long now = millis();

    // 1秒以内に点灯指定されたドットだけをONにする
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            if (dotOnTime[x][y] > 0 && (now - dotOnTime[x][y] < DISPLAY_DURATION)) {
                matrix.drawPixel(x, y, LED_ON);
            } else if (dotOnTime[x][y] > 0) {
                dotOnTime[x][y] = 0; // 1秒過ぎたら消灯データとしてリセット
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
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    // 座標の受信処理（触れた時刻を記録）
    server.on("/draw", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("x") && request->hasParam("y")) {
            int x = request->getParam("x")->value().toInt();
            int y = request->getParam("y")->value().toInt();

            if (x >= 0 && x < 8 && y >= 0 && y < 8) {
                dotOnTime[x][y] = millis(); // 現在の時刻を保存
            }
        }
        request->send(200, "text/plain", "OK");
    });

    server.begin();
}

void loop() {
    // 常に時間の経過をチェックして、1秒経過したドットを消去・再描画
    updateDisplay();
    delay(20); // 画面更新間隔（約50FPS）
}