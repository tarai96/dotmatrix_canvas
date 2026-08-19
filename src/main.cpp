#include <Arduino.h>
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

// ドット（8x8）の状態を保持する配列 (0:消灯, 1:点灯)
uint8_t ledGrid[8][8] = {0};

// スマホ操作用のHTML/JavaScript（キャンバス操作対応）
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <meta charset="utf-8">
    <title>LED Pad</title>
    <style>
        body { text-align: center; font-family: sans-serif; background: #121212; color: white; padding: 20px 10px; user-select: none; touch-action: none; }
        h1 { font-size: 20px; color: #00E676; margin-bottom: 10px; }
        .canvas-container { margin: 20px auto; width: 280px; height: 280px; }
        canvas { background: #222; border: 4px solid #333; border-radius: 12px; touch-action: none; }
        .btn-clear {
            width: 200px; padding: 12px; font-size: 16px; font-weight: bold;
            border-radius: 8px; border: none; background: #FF5252; color: white; cursor: pointer;
        }
    </style>
</head>
<body>
    <h1>LED ドットお絵描き</h1>
    <p style="font-size: 12px; color: #aaa;">画面を指でなぞるとLEDが光ります</p>
    
    <div class="canvas-container">
        <canvas id="pad" width="280" height="280"></canvas>
    </div>

    <button class="btn-clear" onclick="clearCanvas()">クリア（全部消す）</button>

    <script>
        const canvas = document.getElementById('pad');
        const ctx = canvas.getContext('2d');
        const gridSize = 8;
        const cellSize = canvas.width / gridSize;
        let isDrawing = false;
        let gridState = Array(8).fill().map(() => Array(8).fill(0));

        // 8x8のグリッド描画
        function drawGrid() {
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            for (let x = 0; x < gridSize; x++) {
                for (let y = 0; y < gridSize; y++) {
                    ctx.strokeStyle = '#333';
                    ctx.strokeRect(x * cellSize, y * cellSize, cellSize, cellSize);
                    if (gridState[x][y] === 1) {
                        ctx.fillStyle = '#00E676';
                        ctx.fillRect(x * cellSize + 2, y * cellSize + 2, cellSize - 4, cellSize - 4);
                    }
                }
            }
        }

        // 指やマウスの座標から8x8セルを特定して点灯
        function handlePointer(e) {
            const rect = canvas.getBoundingClientRect();
            const clientX = e.touches ? e.touches[0].clientX : e.clientX;
            const clientY = e.touches ? e.touches[0].clientY : e.clientY;

            const x = Math.floor((clientX - rect.left) / cellSize);
            const y = Math.floor((clientY - rect.top) / cellSize);

            if (x >= 0 && x < 8 && y >= 0 && y < 8) {
                if (gridState[x][y] !== 1) {
                    gridState[x][y] = 1;
                    drawGrid();
                    sendDot(x, y, 1);
                }
            }
        }

        // ESP32へ点灯したドットの座標を送る (Fetch API)
        function sendDot(x, y, state) {
            fetch(`/draw?x=${x}&y=${y}&state=${state}`);
        }

        function clearCanvas() {
            gridState = Array(8).fill().map(() => Array(8).fill(0));
            drawGrid();
            fetch('/clear');
        }

        // イベントリスナー（タッチ・マウス両対応）
        canvas.addEventListener('pointerdown', (e) => { isDrawing = true; handlePointer(e); });
        canvas.addEventListener('pointermove', (e) => { if (isDrawing) handlePointer(e); });
        window.addEventListener('pointerup', () => { isDrawing = false; });

        drawGrid();
    </script>
</body>
</html>
)rawliteral";

// ドットマトリクスの表示更新（上下反転の補正適用）
void updateDisplay() {
    matrix.clear();
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            if (ledGrid[x][y] == 1) {
                matrix.drawPixel(x, y, LED_ON);
            }
        }
    }

    // 前回調整した「上下反転」補正処理
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

    // ドット点灯リクエストのリレー (/draw?x=0&y=0&state=1)
    server.on("/draw", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("x") && request->hasParam("y") && request->hasParam("state")) {
            int x = request->getParam("x")->value().toInt();
            int y = request->getParam("y")->value().toInt();
            int state = request->getParam("state")->value().toInt();

            if (x >= 0 && x < 8 && y >= 0 && y < 8) {
                ledGrid[x][y] = state;
                updateDisplay();
            }
        }
        request->send(200, "text/plain", "OK");
    });

    // クリアリクエスト (/clear)
    server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request){
        memset(ledGrid, 0, sizeof(ledGrid));
        updateDisplay();
        request->send(200, "text/plain", "OK");
    });

    server.begin();
}

void loop() {
    // 非同期Webサーバーのため空でOKです
}