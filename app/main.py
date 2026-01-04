import tkinter as tk
from tkinter import ttk
import cv2
from PIL import Image, ImageTk
import requests
import threading
import time


class ESP32CameraController:
    def __init__(self, root, ip_address="192.168.4.1"):
        self.root = root
        self.ip_address = ip_address
        self.base_url = f"http://{ip_address}"

        self.current_speed = 100
        self.nitro_active = False
        self.nitro_timeout = None
        self.is_streaming = False
        self.cap = None

        self.setup_ui()
        self.connect_camera()

    def setup_ui(self):
        """Arayüzü oluştur"""
        self.root.title(f"ESP32 Kamera Kontrol - {self.ip_address}")
        self.root.geometry("800x700")

        # Ana frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))

        # Başlık
        title_label = ttk.Label(main_frame, text="ESP32 Robot Kontrol Paneli",
                                font=("Arial", 16, "bold"))
        title_label.grid(row=0, column=0, columnspan=3, pady=10)

        # Kamera görüntüsü
        self.video_label = ttk.Label(main_frame, background="black")
        self.video_label.grid(row=1, column=0, columnspan=3, pady=10)

        # Hız kontrolü
        speed_frame = ttk.LabelFrame(main_frame, text="Hız Kontrolü", padding="10")
        speed_frame.grid(row=2, column=0, columnspan=3, pady=10, sticky=(tk.W, tk.E))

        # Hız skalası
        self.speed_var = tk.IntVar(value=self.current_speed)
        speed_scale = ttk.Scale(speed_frame, from_=50, to=255,
                                variable=self.speed_var,
                                command=self.on_speed_change)
        speed_scale.grid(row=0, column=0, columnspan=3, sticky=(tk.W, tk.E))

        # Hız değeri
        self.speed_label = ttk.Label(speed_frame, text=f"Hız: {self.current_speed}")
        self.speed_label.grid(row=1, column=1, pady=5)

        # Nitro butonu
        self.nitro_button = ttk.Button(speed_frame, text="🚀 NİTRO (10sn)",
                                       command=self.toggle_nitro)
        self.nitro_button.grid(row=1, column=2, padx=10)

        # Hareket kontrolleri
        control_frame = ttk.LabelFrame(main_frame, text="Hareket Kontrolleri", padding="15")
        control_frame.grid(row=3, column=0, columnspan=3, pady=10)

        # İleri butonu
        self.forward_btn = ttk.Button(control_frame, text="↑\nİLERİ",
                                      width=10, style="Movement.TButton")
        self.forward_btn.grid(row=0, column=1, padx=5, pady=5)
        self.forward_btn.bind('<ButtonPress-1>', lambda e: self.move_forward())
        self.forward_btn.bind('<ButtonRelease-1>', lambda e: self.stop())

        # Sol butonu
        self.left_btn = ttk.Button(control_frame, text="← SOL",
                                   width=10, style="Movement.TButton")
        self.left_btn.grid(row=1, column=0, padx=5, pady=5)
        self.left_btn.bind('<ButtonPress-1>', lambda e: self.move_left())
        self.left_btn.bind('<ButtonRelease-1>', lambda e: self.stop())

        # Dur butonu
        self.stop_btn = ttk.Button(control_frame, text="DUR",
                                   width=10, style="Stop.TButton",
                                   command=self.stop)
        self.stop_btn.grid(row=1, column=1, padx=5, pady=5)

        # Sağ butonu
        self.right_btn = ttk.Button(control_frame, text="SAĞ →",
                                    width=10, style="Movement.TButton")
        self.right_btn.grid(row=1, column=2, padx=5, pady=5)
        self.right_btn.bind('<ButtonPress-1>', lambda e: self.move_right())
        self.right_btn.bind('<ButtonRelease-1>', lambda e: self.stop())

        # Geri butonu
        self.backward_btn = ttk.Button(control_frame, text="↓\nGERİ",
                                       width=10, style="Movement.TButton")
        self.backward_btn.grid(row=2, column=1, padx=5, pady=5)
        self.backward_btn.bind('<ButtonPress-1>', lambda e: self.move_backward())
        self.backward_btn.bind('<ButtonRelease-1>', lambda e: self.stop())

        # Durum bilgisi
        status_frame = ttk.Frame(main_frame)
        status_frame.grid(row=4, column=0, columnspan=3, pady=10)

        self.status_label = ttk.Label(status_frame, text="Hazır",
                                      foreground="blue", font=("Arial", 10))
        self.status_label.grid(row=0, column=0)

        # Buton stilleri
        style = ttk.Style()
        style.configure("Movement.TButton", font=("Arial", 12, "bold"))
        style.configure("Stop.TButton", font=("Arial", 12, "bold"), background="#ff4444")

        # Klavye kontrolleri
        self.root.bind('<KeyPress>', self.on_key_press)
        self.root.bind('<KeyRelease>', self.on_key_release)
        self.root.focus_set()

    def connect_camera(self):
        """Kameraya bağlan"""
        try:
            self.cap = cv2.VideoCapture(f"{self.base_url}/stream")
            if self.cap.isOpened():
                self.is_streaming = True
                self.update_status("Kamera bağlantısı başarılı!", "green")
                # Görüntü güncelleme thread'ini başlat
                self.update_thread = threading.Thread(target=self.update_frame, daemon=True)
                self.update_thread.start()
            else:
                self.update_status("Kameraya bağlanılamadı!", "red")
        except Exception as e:
            self.update_status(f"Kamera hatası: {str(e)}", "red")

    def update_frame(self):
        """Görüntüyü güncelle"""
        while self.is_streaming:
            try:
                ret, frame = self.cap.read()
                if ret:
                    # OpenCV BGR formatını RGB'ye çevir
                    frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

                    # PIL Image'a çevir
                    image = Image.fromarray(frame_rgb)

                    # Boyutlandır
                    image = image.resize((640, 480), Image.Resampling.LANCZOS)

                    # Tkinter için uygun formata çevir
                    photo = ImageTk.PhotoImage(image)

                    # GUI'de göster
                    self.video_label.config(image=photo)
                    self.video_label.image = photo

                time.sleep(0.03)  # ~30 FPS

            except Exception as e:
                self.update_status(f"Görüntü hatası: {str(e)}", "red")
                time.sleep(1)

    def send_command(self, command, speed=None):
        """ESP32'ye komut gönder"""
        if speed is None:
            speed = self.current_speed

        try:
            url = f"{self.base_url}/api/{command}?speed={speed}"
            response = requests.get(url, timeout=2)
            self.update_status(f"Komut: {command.upper()}, Hız: {speed}", "blue")
            return response.text
        except requests.exceptions.RequestException as e:
            self.update_status(f"Komut hatası: {command}", "red")
            return None

    def move_forward(self):
        self.send_command("forward")

    def move_backward(self):
        self.send_command("backward")

    def move_left(self):
        self.send_command("left")

    def move_right(self):
        self.send_command("right")

    def stop(self):
        self.send_command("stop")

    def on_speed_change(self, value):
        """Hız değişikliğini işle"""
        self.current_speed = int(float(value))
        self.speed_label.config(text=f"Hız: {self.current_speed}")

    def toggle_nitro(self):
        """Nitro'yu aç/kapat"""
        if not self.nitro_active:
            # Nitro'yu aç
            self.nitro_active = True
            old_speed = self.current_speed
            self.current_speed = 255
            self.speed_var.set(255)
            self.nitro_button.config(text="🚀 NİTRO AKTİF")
            self.update_status("NİTRO AKTİF! Hız: 255", "red")

            # 10 saniye sonra nitro'yu kapat
            self.nitro_timeout = threading.Timer(10.0, self.deactivate_nitro)
            self.nitro_timeout.start()
        else:
            # Nitro'yu kapat
            self.deactivate_nitro()

    def deactivate_nitro(self):
        """Nitro'yu kapat"""
        if self.nitro_active:
            self.nitro_active = False
            self.current_speed = 100
            self.speed_var.set(100)
            self.nitro_button.config(text="🚀 NİTRO (10sn)")
            self.update_status("Nitro kapatıldı. Hız: 100", "green")

            if self.nitro_timeout:
                self.nitro_timeout.cancel()

            self.stop()

    def on_key_press(self, event):
        """Klavye tuş basılı tutma"""
        key = event.keysym.lower()

        if key == 'w':
            self.move_forward()
        elif key == 's':
            self.move_backward()
        elif key == 'a':
            self.move_left()
        elif key == 'd':
            self.move_right()
        elif key == 'n':
            self.toggle_nitro()
        elif key == 'space':
            self.stop()

    def on_key_release(self, event):
        """Klavye tuş bırakma"""
        key = event.keysym.lower()

        if key in ['w', 's', 'a', 'd']:
            self.stop()

    def update_status(self, message, color="black"):
        """Durum mesajını güncelle"""
        self.status_label.config(text=message, foreground=color)
        print(f"Durum: {message}")

    def cleanup(self):
        """Kaynakları temizle"""
        self.is_streaming = False
        self.deactivate_nitro()
        self.stop()

        if self.cap:
            self.cap.release()

        # 1 saniye bekle ve çık
        self.root.after(1000, self.root.destroy)


# Basit versiyon (daha az özellik)
class SimpleCameraController:
    def __init__(self, root, ip_address="192.168.4.1"):
        self.root = root
        self.ip_address = ip_address
        self.base_url = f"http://{ip_address}"
        self.speed = 100

        self.setup_simple_ui()

    def setup_simple_ui(self):
        """Basit arayüz oluştur"""
        self.root.title("ESP32 Kontrol")
        self.root.geometry("400x500")

        main_frame = ttk.Frame(self.root, padding="20")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Butonlar
        btn_style = {"width": 8, "height": 2, "font": ("Arial", 12)}

        ttk.Label(main_frame, text="ESP32 KONTROL", font=("Arial", 14, "bold")).pack(pady=10)

        # İleri
        ttk.Button(main_frame, text="↑ İLERİ", **btn_style,
                   command=lambda: self.send_cmd("forward")).pack(pady=5)

        # Sol ve Sağ
        row_frame = ttk.Frame(main_frame)
        row_frame.pack(pady=5)

        ttk.Button(row_frame, text="← SOL", **btn_style,
                   command=lambda: self.send_cmd("left")).pack(side=tk.LEFT, padx=5)

        ttk.Button(row_frame, text="SAĞ →", **btn_style,
                   command=lambda: self.send_cmd("right")).pack(side=tk.LEFT, padx=5)

        # Geri
        ttk.Button(main_frame, text="↓ GERİ", **btn_style,
                   command=lambda: self.send_cmd("backward")).pack(pady=5)

        # Nitro
        ttk.Button(main_frame, text="🚀 NİTRO", **btn_style,
                   command=self.toggle_nitro).pack(pady=10)

        # Hız kontrolü
        speed_frame = ttk.Frame(main_frame)
        speed_frame.pack(pady=10)

        ttk.Label(speed_frame, text="Hız:").pack(side=tk.LEFT)
        self.speed_var = tk.StringVar(value="100")
        speed_combo = ttk.Combobox(speed_frame, textvariable=self.speed_var,
                                   values=["50", "75", "100", "150", "200", "255"],
                                   state="readonly", width=5)
        speed_combo.pack(side=tk.LEFT, padx=5)
        speed_combo.bind('<<ComboboxSelected>>', self.on_speed_change)

        # Durum
        self.status_label = ttk.Label(main_frame, text="Hazır")
        self.status_label.pack(pady=10)

        # Stil
        style = ttk.Style()
        style.configure("Stop.TButton", background="#ff4444")

    def send_cmd(self, command):
        """Komut gönder"""
        try:
            url = f"{self.base_url}/api/{command}?speed={self.speed}"
            requests.get(url, timeout=1)
            self.status_label.config(text=f"{command.upper()} - Hız: {self.speed}")
        except:
            self.status_label.config(text="Bağlantı hatası!")

    def on_speed_change(self, event):
        """Hız değişikliği"""
        self.speed = self.speed_var.get()

    def toggle_nitro(self):
        """Nitro aç/kapat"""
        old_speed = self.speed
        self.speed = "255"
        self.status_label.config(text=f"NİTRO AKTİF! Hız: 255")

        # 10 saniye sonra normale dön
        self.root.after(10000, lambda: self.deactivate_nitro(old_speed))

    def deactivate_nitro(self, old_speed):
        """Nitro'yu kapat"""
        self.speed = old_speed
        self.speed_var.set(old_speed)
        self.status_label.config(text=f"Nitro bitti. Hız: {old_speed}")


# Ana uygulama
if __name__ == "__main__":
    root = tk.Tk()

    # Gelişmiş versiyon
    app = ESP32CameraController(root, "192.168.4.1")

    # Veya basit versiyon
    # app = SimpleCameraController(root, "192.168.4.1")

    # Pencere kapanırken temizlik
    root.protocol("WM_DELETE_WINDOW", app.cleanup)

    root.mainloop()
