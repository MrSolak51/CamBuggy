import cv2
import threading
import requests
from tkinter import *
from tkinter import ttk

from PIL import Image, ImageTk

STREAM_URL = "http://192.168.1.2:81/stream" # Kamera akış URL'si
CONTROL_URL = "http://192.168.1.2"  # Kontrol URL'si

def send_command(command):
    try:
        url = f"{CONTROL_URL}/action?move={command}"
        print(f"Sending: {url}")
        requests.get(url, timeout=0.5)
    except:
        print("Command failed.")
def create_buttons(root):
    frame = Frame(root)
    
    frame.columnconfigure(0, weight=2)

    frame.rowconfigure(0, weight=2)
    frame.rowconfigure(1, weight=1)

    rot_frame = Frame(frame)
    rot_frame.columnconfigure(0, weight=1)
    rot_frame.columnconfigure(1, weight=1)
    rot_frame.columnconfigure(2, weight=1)
    rot_frame.rowconfigure(0, weight=1)
    rot_frame.rowconfigure(1, weight=1)



    rot_frame.grid(row=0, column=0, sticky="nsew")
    frame.pack(expand=True, fill=BOTH)

    forward_icon = PhotoImage(file="img/arrow-up-solid.png")
    backward_icon = PhotoImage(file="img/arrow-down-solid.png")
    left_icon = PhotoImage(file="img/arrow-left-solid.png")
    right_icon = PhotoImage(file="img/arrow-right-solid.png")
    nitro_icon = PhotoImage(file="img/square-caret-up-regular.png")

    # Basılınca hareket, bırakınca dur
    def bind_button(widget, command_name):
        widget.bind("<ButtonPress>", lambda e: send_command(command_name))
        widget.bind("<ButtonRelease>", lambda e: send_command("stop"))

    forward_button = Button(rot_frame, width=10, image=forward_icon)
    forward_button.grid(row=0, column=1, sticky="nsew")
    bind_button(forward_button, "forward")

    left_button = Button(rot_frame, width=10, image=left_icon)
    left_button.grid(row=1, column=0, sticky="nsew")
    bind_button(left_button, "left")

    right_button = Button(rot_frame, width=10, image=right_icon)
    right_button.grid(row=1, column=2, sticky="nsew")
    bind_button(right_button, "right")

    backward_button = Button(rot_frame, width=10, image=backward_icon)
    backward_button.grid(row=1, column=1, sticky="nsew")
    bind_button(backward_button, "backward")

    ttk.Checkbutton(frame, width=10,image=nitro_icon, command=lambda: send_command("nitro")).grid(row=1, column=0, sticky="nsew")

def video_loop(label):
    cap = cv2.VideoCapture(STREAM_URL)
    if not cap.isOpened():
        print("Error: Unable to open video stream.")
        return

    while True:
        ret, frame = cap.read()
        if not ret:
            continue

        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        img = Image.fromarray(frame)
        imgtk = ImageTk.PhotoImage(image=img)
        label.imgtk = imgtk
        label.configure(image=imgtk)

def main():
    root = Tk()
    root.title("R6 Drone Control")

    video_label = Label(root)
    video_label.pack()

    create_buttons(root)

    threading.Thread(target=video_loop, args=(video_label,), daemon=True).start()

    root.mainloop()

if __name__ == "__main__":
    main()
