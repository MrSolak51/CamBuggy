from flask import Flask, render_template_string, Response, request, jsonify
import requests
import cv2
import numpy as np
import threading
import time

app = Flask(__name__)

ESP_IP = "192.168.4.1"
BASE_URL = f"http://{ESP_IP}"

# Görüntü buffer'ı
current_frame = None
frame_lock = threading.Lock()


def get_image_from_esp():
    """ESP32'den görüntü al"""
    try:
        response = requests.get(f"{BASE_URL}/image", timeout=1.0)
        if response.status_code == 200:
            img_format = int(response.headers.get('X-Format', 1))
            width = int(response.headers.get('X-Width', 160))
            height = int(response.headers.get('X-Height', 120))

            raw_data = np.frombuffer(response.content, dtype=np.uint8)

            if img_format == 1:  # GRAYSCALE
                img = raw_data.reshape((height, width))
                return cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
    except:
        pass
    return None


def image_updater():
    """Görüntüyü sürekli güncelle"""
    global current_frame
    while True:
        img = get_image_from_esp()
        if img is not None:
            with frame_lock:
                current_frame = img
        time.sleep(0.1)


def generate_frames():
    """Video stream oluştur"""
    while True:
        with frame_lock:
            if current_frame is not None:
                # JPEG'e çevir
                ret, buffer = cv2.imencode('.jpg', current_frame,
                                           [cv2.IMWRITE_JPEG_QUALITY, 70])
                if ret:
                    frame = buffer.tobytes()
                    yield (b'--frame\r\n'
                           b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')
        time.sleep(0.033)


@app.route('/')
def index():
    """Ana sayfa"""
    html = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>CamBuggy Web Control</title>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <style>
            body {
                font-family: Arial, sans-serif;
                text-align: center;
                padding: 20px;
                background: #f0f0f0;
            }
            .container {
                max-width: 800px;
                margin: 0 auto;
                background: white;
                padding: 20px;
                border-radius: 10px;
                box-shadow: 0 0 10px rgba(0,0,0,0.1);
            }
            img {
                width: 100%;
                max-width: 480px;
                border: 2px solid #333;
                border-radius: 5px;
                margin: 10px 0;
            }
            .controls {
                margin: 20px 0;
            }
            button {
                padding: 15px 25px;
                margin: 5px;
                font-size: 16px;
                border: none;
                border-radius: 5px;
                cursor: pointer;
                background: #4CAF50;
                color: white;
                min-width: 100px;
            }
            button:hover {
                opacity: 0.9;
            }
            .stop-btn {
                background: #f44336;
            }
            .status {
                background: #e8f5e8;
                padding: 10px;
                border-radius: 5px;
                margin: 10px 0;
            }
            .speed-control {
                margin: 15px 0;
            }
            input[type="range"] {
                width: 60%;
                margin: 0 10px;
            }
        </style>
    </head>
    <body>
        <div class="container">
            <h1>🚗 CamBuggy Web Control</h1>

            <div class="status">
                <div>ESP32 IP: <strong>""" + ESP_IP + """</strong></div>
                <div>Status: <span id="status">Connected</span></div>
            </div>

            <img id="video" src="/video_feed">

            <div class="speed-control">
                <label>Speed: <span id="speedValue">150</span></label>
                <input type="range" id="speed" min="50" max="255" value="150" 
                       oninput="updateSpeed(this.value)">
            </div>

            <div class="controls">
                <button onclick="move('forward')">↑ Forward</button><br>
                <button onclick="move('left')">← Left</button>
                <button class="stop-btn" onclick="stopMotors()">⏹ Stop</button>
                <button onclick="move('right')">→ Right</button><br>
                <button onclick="move('backward')">↓ Backward</button>
            </div>

            <div>
                <button onclick="moveCustom(200, 200)">Fast Forward</button>
                <button onclick="moveCustom(-200, -200)">Fast Backward</button>
                <button onclick="moveCustom(150, -150)">Spin Right</button>
                <button onclick="moveCustom(-150, 150)">Spin Left</button>
            </div>

            <p>Press and hold arrow keys for continuous movement</p>
        </div>

        <script>
            let speed = 150;

            function updateSpeed(value) {
                speed = parseInt(value);
                document.getElementById('speedValue').innerText = value;
            }

            function move(direction) {
                let right = speed, left = speed;

                switch(direction) {
                    case 'forward':
                        break;
                    case 'backward':
                        right = -speed;
                        left = -speed;
                        break;
                    case 'left':
                        right = speed;
                        left = speed * 0.5;
                        break;
                    case 'right':
                        right = speed * 0.5;
                        left = speed;
                        break;
                }

                fetch('/api/move?right=' + right + '&left=' + left)
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('status').innerText = 
                            'Motors: R=' + right + ', L=' + left;
                    });
            }

            function moveCustom(right, left) {
                fetch('/api/move?right=' + right + '&left=' + left)
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('status').innerText = 
                            'Motors: R=' + right + ', L=' + left;
                    });
            }

            function stopMotors() {
                fetch('/api/stop')
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('status').innerText = 'Stopped';
                    });
            }

            // Keyboard controls
            document.addEventListener('keydown', (e) => {
                switch(e.key) {
                    case 'ArrowUp': move('forward'); break;
                    case 'ArrowDown': move('backward'); break;
                    case 'ArrowLeft': move('left'); break;
                    case 'ArrowRight': move('right'); break;
                    case ' ': stopMotors(); break;
                }
            });

            // Auto-refresh image every 100ms
            setInterval(() => {
                document.getElementById('video').src = '/video_feed?t=' + Date.now();
            }, 100);
        </script>
    </body>
    </html>
    """
    return render_template_string(html)


@app.route('/video_feed')
def video_feed():
    """Video stream endpoint"""
    return Response(generate_frames(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')


@app.route('/api/move')
def api_move():
    """Motor kontrol API"""
    right = request.args.get('right', type=int)
    left = request.args.get('left', type=int)

    if right is not None and left is not None:
        try:
            response = requests.get(f"{BASE_URL}/move?right={right}&left={left}",
                                    timeout=0.5)
            return jsonify({'status': 'ok', 'message': response.text})
        except Exception as e:
            return jsonify({'status': 'error', 'message': str(e)})

    return jsonify({'status': 'error', 'message': 'Invalid parameters'})


@app.route('/api/stop')
def api_stop():
    """Motor durdurma API"""
    try:
        response = requests.get(f"{BASE_URL}/stop", timeout=0.5)
        return jsonify({'status': 'ok', 'message': response.text})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)})


@app.route('/api/status')
def api_status():
    """Sistem durumu"""
    try:
        response = requests.get(f"{BASE_URL}/", timeout=1)
        return jsonify({'status': 'connected', 'ip': ESP_IP})
    except:
        return jsonify({'status': 'disconnected', 'ip': ESP_IP})


if __name__ == '__main__':
    # Görüntü güncelleme thread'ini başlat
    update_thread = threading.Thread(target=image_updater, daemon=True)
    update_thread.start()

    print("🚀 Starting CamBuggy Web Control")
    print(f"📡 ESP32 IP: {ESP_IP}")
    print("🌐 Web interface: http://localhost:5000")
    print("\n🎮 Controls:")
    print("  Web interface - Full control")
    print("  Arrow keys - Movement")
    print("  Spacebar - Stop")

    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)