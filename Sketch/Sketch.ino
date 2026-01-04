#include "esp_camera.h"
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WebServer.h>

// WiFi ayarları
const char* ssid = "CamBuggy";
const char* password = "12345678";

WebServer server(80);

// ESP32-CAM pinleri (M21-45 için)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Motor pinleri
#define MOTOR_RIGHT_FWD   14
#define MOTOR_RIGHT_BWD   15
#define MOTOR_LEFT_FWD    12
#define MOTOR_LEFT_BWD    13
#define MOTOR_RIGHT_PWM   2    // GPIO2
#define MOTOR_LEFT_PWM    16   // GPIO16

// LED Flash pin
#define LED_FLASH         4

// Değişkenler
unsigned long lastFrameTime = 0;
int frameCount = 0;
int fps = 0;
int motorRightSpeed = 0;
int motorLeftSpeed = 0;
bool flashState = false;
bool cameraInitialized = false;

// Motor PWM kurulumu
void setupMotorPWM() {
    ledcSetup(0, 5000, 8);  // Kanal 0, 5kHz, 8-bit
    ledcSetup(1, 5000, 8);  // Kanal 1, 5kHz, 8-bit
    
    ledcAttachPin(MOTOR_RIGHT_PWM, 0);
    ledcAttachPin(MOTOR_LEFT_PWM, 1);
    
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    
    Serial.println("✅ PWM setup complete");
}

// Motor pinleri kurulumu
void setupMotorPins() {
    pinMode(MOTOR_RIGHT_FWD, OUTPUT);
    pinMode(MOTOR_RIGHT_BWD, OUTPUT);
    pinMode(MOTOR_LEFT_FWD, OUTPUT);
    pinMode(MOTOR_LEFT_BWD, OUTPUT);
    
    digitalWrite(MOTOR_RIGHT_FWD, LOW);
    digitalWrite(MOTOR_RIGHT_BWD, LOW);
    digitalWrite(MOTOR_LEFT_FWD, LOW);
    digitalWrite(MOTOR_LEFT_BWD, LOW);
    
    Serial.println("✅ Motor pins setup complete");
}

// Flash LED kurulumu
void setupFlash() {
    pinMode(LED_FLASH, OUTPUT);
    digitalWrite(LED_FLASH, LOW);
    Serial.println("✅ Flash setup complete");
}

// Motor kontrol fonksiyonu
void moveMotors(int rightSpeed, int leftSpeed) {
    // Sağ motor kontrolü
    if (rightSpeed > 0) {
        digitalWrite(MOTOR_RIGHT_FWD, HIGH);
        digitalWrite(MOTOR_RIGHT_BWD, LOW);
        motorRightSpeed = constrain(rightSpeed, 0, 255);
    } else if (rightSpeed < 0) {
        digitalWrite(MOTOR_RIGHT_FWD, LOW);
        digitalWrite(MOTOR_RIGHT_BWD, HIGH);
        motorRightSpeed = constrain(-rightSpeed, 0, 255);
    } else {
        digitalWrite(MOTOR_RIGHT_FWD, LOW);
        digitalWrite(MOTOR_RIGHT_BWD, LOW);
        motorRightSpeed = 0;
    }
    
    // Sol motor kontrolü
    if (leftSpeed > 0) {
        digitalWrite(MOTOR_LEFT_FWD, HIGH);
        digitalWrite(MOTOR_LEFT_BWD, LOW);
        motorLeftSpeed = constrain(leftSpeed, 0, 255);
    } else if (leftSpeed < 0) {
        digitalWrite(MOTOR_LEFT_FWD, LOW);
        digitalWrite(MOTOR_LEFT_BWD, HIGH);
        motorLeftSpeed = constrain(-leftSpeed, 0, 255);
    } else {
        digitalWrite(MOTOR_LEFT_FWD, LOW);
        digitalWrite(MOTOR_LEFT_BWD, LOW);
        motorLeftSpeed = 0;
    }
    
    // PWM değerlerini uygula
    ledcWrite(0, motorRightSpeed);
    ledcWrite(1, motorLeftSpeed);
    
    Serial.printf("Motors: R=%d (PWM=%d), L=%d (PWM=%d)\n", 
                  rightSpeed, motorRightSpeed, leftSpeed, motorLeftSpeed);
}

// Motorları durdur
void stopMotors() {
    moveMotors(0, 0);
    Serial.println("Motors stopped");
}

// Flash'ı kontrol et
void setFlash(bool state) {
    flashState = state;
    digitalWrite(LED_FLASH, state ? HIGH : LOW);
    Serial.printf("Flash: %s\n", state ? "ON" : "OFF");
}

// Flash'ı toggle et
void toggleFlash() {
    flashState = !flashState;
    setFlash(flashState);
}

// API Handler'lar
void handleMove() {
    if (server.hasArg("right") && server.hasArg("left")) {
        int rightSpeed = server.arg("right").toInt();
        int leftSpeed = server.arg("left").toInt();
        
        if (server.hasArg("right_dir")) {
            String rightDir = server.arg("right_dir");
            if (rightDir == "backward") rightSpeed = -rightSpeed;
        }
        
        if (server.hasArg("left_dir")) {
            String leftDir = server.arg("left_dir");
            if (leftDir == "backward") leftSpeed = -leftSpeed;
        }
        
        moveMotors(rightSpeed, leftSpeed);
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing parameters");
    }
}

void handleStop() {
    stopMotors();
    server.send(200, "text/plain", "Stopped");
}

void handleFlash() {
    toggleFlash();
    server.send(200, "text/plain", flashState ? "ON" : "OFF");
}

void handleStatus() {
    String json = "{";
    json += "\"fps\":" + String(fps) + ",";
    json += "\"motor_right\":" + String(motorRightSpeed) + ",";
    json += "\"motor_left\":" + String(motorLeftSpeed) + ",";
    json += "\"flash\":" + String(flashState ? "true" : "false") + ",";
    json += "\"camera\":" + String(cameraInitialized ? "true" : "false");
    json += "}";
    
    server.send(200, "application/json", json);
}

// Basit görüntü yakalama - doğrudan JPEG'e dönüştür
void handleImage() {
    if (!cameraInitialized) {
        server.send(503, "text/plain", "Camera not available");
        return;
    }
    
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        server.send(500, "text/plain", "Camera capture failed");
        return;
    }
    
    // Doğrudan frame buffer'ı gönder (GRAYSCALE formatında)
    // Tarayıcıya uygun format için PPM formatına dönüştürelim
    String ppmHeader = "P5\n" + String(fb->width) + " " + String(fb->height) + "\n255\n";
    
    server.setContentLength(ppmHeader.length() + fb->len);
    server.send(200, "image/x-portable-pixmap");
    server.client().print(ppmHeader);
    server.client().write(fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
    
    // FPS hesapla
    frameCount++;
    if (millis() - lastFrameTime >= 1000) {
        fps = frameCount;
        frameCount = 0;
        lastFrameTime = millis();
    }
}

// Ana sayfa handler'ı
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CamBuggy Control</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background: #f0f0f0;
            padding: 20px;
            margin: 0;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            margin-bottom: 20px;
        }
        .video-container {
            margin: 20px 0;
        }
        #stream {
            width: 100%;
            max-width: 320px;
            border: 2px solid #333;
            border-radius: 5px;
        }
        .control-panel {
            margin: 20px 0;
            padding: 20px;
            background: #f8f8f8;
            border-radius: 5px;
        }
        .button-row {
            display: flex;
            justify-content: center;
            gap: 10px;
            margin: 10px 0;
            flex-wrap: wrap;
        }
        button {
            padding: 12px 24px;
            font-size: 16px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            background: #4CAF50;
            color: white;
            min-width: 100px;
        }
        button:hover {
            background: #45a049;
        }
        button.stop {
            background: #f44336;
        }
        button.stop:hover {
            background: #d32f2f;
        }
        button.flash {
            background: #FF9800;
        }
        button.flash:hover {
            background: #F57C00;
        }
        .status {
            background: #e8f5e8;
            padding: 10px;
            border-radius: 5px;
            margin: 10px 0;
        }
        .slider-container {
            margin: 15px 0;
        }
        .slider-container label {
            display: block;
            margin-bottom: 5px;
        }
        input[type="range"] {
            width: 80%;
        }
        .info {
            background: #e3f2fd;
            padding: 10px;
            border-radius: 5px;
            margin: 10px 0;
            font-size: 14px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚗 CamBuggy Control Panel</h1>
        
        <div class="info">
            <strong>Connected to:</strong> CamBuggy<br>
            <strong>IP:</strong> 192.168.4.1<br>
            <strong>Sensor:</strong> OV7670 (160x120 GRAYSCALE)
        </div>
        
        <div class="status">
            <div id="fps">FPS: 0</div>
            <div>Motor Right: <span id="motorR">0</span></div>
            <div>Motor Left: <span id="motorL">0</span></div>
            <div>Flash: <span id="flashStatus">OFF</span></div>
            <div>Camera: <span id="cameraStatus">OK</span></div>
        </div>
        
        <div class="video-container">
            <canvas id="streamCanvas" width="160" height="120"></canvas>
            <div class="button-row">
                <button onclick="refreshImage()">🔄 Refresh</button>
                <button class="flash" onclick="toggleFlash()">⚡ Flash</button>
            </div>
        </div>
        
        <div class="control-panel">
            <h2>🎮 Motor Control</h2>
            
            <!-- Simple Controls -->
            <div class="button-row">
                <button onclick="moveForward()">↑ Forward</button>
            </div>
            <div class="button-row">
                <button onclick="moveLeft()">← Left</button>
                <button class="stop" onclick="stopMotors()">⏹ Stop</button>
                <button onclick="moveRight()">→ Right</button>
            </div>
            <div class="button-row">
                <button onclick="moveBackward()">↓ Backward</button>
            </div>
            
            <!-- Manual Speed Control -->
            <div class="slider-container">
                <label>Right Motor Speed: <span id="rightSpeedValue">128</span></label>
                <input type="range" id="rightSpeed" min="0" max="255" value="128" oninput="updateRightSpeed(this.value)">
            </div>
            <div class="slider-container">
                <label>Left Motor Speed: <span id="leftSpeedValue">128</span></label>
                <input type="range" id="leftSpeed" min="0" max="255" value="128" oninput="updateLeftSpeed(this.value)">
            </div>
            
            <!-- Quick Controls -->
            <div class="button-row">
                <button onclick="moveCustom(200, 200)">Fast Forward</button>
                <button onclick="moveCustom(-200, -200)">Fast Backward</button>
            </div>
            <div class="button-row">
                <button onclick="moveCustom(150, -150)">Spin Right</button>
                <button onclick="moveCustom(-150, 150)">Spin Left</button>
            </div>
        </div>
    </div>
    
    <script>
        let rightSpeed = 128;
        let leftSpeed = 128;
        let flashState = false;
        
        function updateStatus() {
            document.getElementById('motorR').textContent = rightSpeed;
            document.getElementById('motorL').textContent = leftSpeed;
            document.getElementById('flashStatus').textContent = flashState ? 'ON' : 'OFF';
        }
        
        function sendMove(r, l, rDir = 'forward', lDir = 'forward') {
            const url = `/api/move?right=${r}&left=${l}&right_dir=${rDir}&left_dir=${lDir}`;
            fetch(url).then(() => updateStatus());
        }
        
        function moveForward() {
            sendMove(rightSpeed, leftSpeed);
        }
        
        function moveBackward() {
            sendMove(rightSpeed, leftSpeed, 'backward', 'backward');
        }
        
        function moveLeft() {
            sendMove(rightSpeed, leftSpeed / 3);
        }
        
        function moveRight() {
            sendMove(rightSpeed / 3, leftSpeed);
        }
        
        function moveCustom(r, l) {
            sendMove(Math.abs(r), Math.abs(l), r < 0 ? 'backward' : 'forward', l < 0 ? 'backward' : 'forward');
        }
        
        function stopMotors() {
            fetch('/api/stop').then(() => {
                rightSpeed = 128;
                leftSpeed = 128;
                updateStatus();
            });
        }
        
        function updateRightSpeed(value) {
            rightSpeed = value;
            document.getElementById('rightSpeedValue').textContent = value;
        }
        
        function updateLeftSpeed(value) {
            leftSpeed = value;
            document.getElementById('leftSpeedValue').textContent = value;
        }
        
        function toggleFlash() {
            fetch('/api/flash')
                .then(response => response.text())
                .then(state => {
                    flashState = state === 'ON';
                    updateStatus();
                });
        }
        
        function refreshImage() {
            fetch('/image')
                .then(response => response.arrayBuffer())
                .then(buffer => {
                    const canvas = document.getElementById('streamCanvas');
                    const ctx = canvas.getContext('2d');
                    const imageData = ctx.createImageData(160, 120);
                    
                    // PPM formatını işle (P5: binary grayscale)
                    const dataView = new DataView(buffer);
                    let offset = 0;
                    
                    // Header'ı atla: "P5\n160 120\n255\n"
                    let headerText = '';
                    while (offset < buffer.byteLength) {
                        const char = String.fromCharCode(dataView.getUint8(offset));
                        headerText += char;
                        offset++;
                        if (headerText.endsWith('\n255\n')) {
                            break;
                        }
                    }
                    
                    // GRAYSCALE verisini canvas'a çiz
                    const data = new Uint8Array(buffer, offset);
                    for (let i = 0; i < data.length; i++) {
                        const pixelValue = data[i];
                        imageData.data[i * 4] = pixelValue;     // R
                        imageData.data[i * 4 + 1] = pixelValue; // G
                        imageData.data[i * 4 + 2] = pixelValue; // B
                        imageData.data[i * 4 + 3] = 255;        // A
                    }
                    
                    ctx.putImageData(imageData, 0, 0);
                })
                .catch(error => {
                    console.error('Error loading image:', error);
                    document.getElementById('cameraStatus').textContent = 'Error';
                });
        }
        
        // Auto-refresh görüntü (2 FPS)
        setInterval(refreshImage, 500);
        
        // FPS güncelleme
        setInterval(() => {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('fps').textContent = 'FPS: ' + data.fps;
                    document.getElementById('cameraStatus').textContent = data.camera ? 'OK' : 'Error';
                });
        }, 2000);
        
        // İlk yükleme
        updateStatus();
        refreshImage();
    </script>
</body>
</html>
)rawliteral";
    
    server.send(200, "text/html", html);
}

void setup() {
    // Brownout detector disable
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n🚀 CamBuggy Control System Starting...");
    Serial.println("Detected: M21-45 with OV7670 sensor (No PSRAM)");
    
    // Bellek durumunu göster
    Serial.printf("Free heap: %d bytes\n", esp_get_free_heap_size());
    
    // Motor ve flash kurulumu
    setupMotorPins();
    setupMotorPWM();
    setupFlash();
    
    Serial.println("✅ Motor controls ready");
    
    // KAMERA KONFİGÜRASYONU - ÇOK DÜŞÜK ÇÖZÜNÜRLÜK
    camera_config_t config;
    
    // Pin tanımları
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    
    // XCLK frekansı
    config.xclk_freq_hz = 4000000; // 4MHz
    
    // LEDC konfigürasyonu
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    
    // Pixel format - en az bellek kullanan
    config.pixel_format = PIXFORMAT_GRAYSCALE; // 1 byte/pixel
    
    // Frame boyutu - ÇOK KÜÇÜK
    config.frame_size = FRAMESIZE_QQVGA; // 160x120 - sadece 19.2KB (GRAYSCALE)
    
    // FB sayısı
    config.fb_count = 1; // Sadece 1 frame buffer
    
    // Kamera başlat
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        cameraInitialized = false;
    } else {
        Serial.println("✅ Camera initialized (160x120 GRAYSCALE)");
        cameraInitialized = true;
        
        // Sensör ayarları
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL) {
            s->set_vflip(s, 0);
            s->set_hmirror(s, 0);
            s->set_brightness(s, 0);
            s->set_contrast(s, 0);
            Serial.println("✅ Camera sensor configured");
        }
    }
    
    // WiFi
    WiFi.softAP(ssid, password);
    delay(100);
    
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("📡 AP IP: ");
    Serial.println(myIP);
    
    // API Route'ları
    server.on("/", handleRoot);
    server.on("/image", handleImage);
    server.on("/api/move", handleMove);
    server.on("/api/stop", handleStop);
    server.on("/api/flash", handleFlash);
    server.on("/api/status", handleStatus);
    
    // 404 handler
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not Found");
    });
    
    server.begin();
    Serial.println("✅ Web server started");
    Serial.println("📱 Open: http://" + myIP.toString());
    
    // Test
    Serial.println("Testing system...");
    delay(1000);
    
    // Test flash
    setFlash(true);
    delay(500);
    setFlash(false);
    
    // Test kamera
    if (cameraInitialized) {
        camera_fb_t* test_fb = esp_camera_fb_get();
        if (test_fb) {
            Serial.printf("Camera test OK! Image size: %d bytes\n", test_fb->len);
            esp_camera_fb_return(test_fb);
        } else {
            Serial.println("Camera test failed");
            cameraInitialized = false;
        }
    }
    
    Serial.println("✅ Setup complete!");
}

void loop() {
    server.handleClient();
    delay(10);
}