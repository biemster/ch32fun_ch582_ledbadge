#!/usr/bin/env python
"""
requires pyusb, which should be pippable
SUBSYSTEM=="usb", ATTR{idVendor}=="1209", ATTR{idProduct}=="d035", MODE="666"
sudo udevadm control --reload-rules && sudo udevadm trigger
"""
import os
import argparse
import usb.core
import usb.util

CH_USB_VENDOR_ID    = 0x1209    # VID
CH_USB_PRODUCT_ID   = 0xd035    # PID
CH_USB_INTERFACE    = 0         # interface number
CH_USB_EP_IN        = 0x81      # endpoint for reply transfer in
CH_USB_EP_OUT       = 0x02      # endpoint for command transfer out
CH_USB_PACKET_SIZE  = 64        # packet size
CH_USB_TIMEOUT_MS   = 2000      # timeout for USB operations

CH_CMD_REBOOT       = 0xa2
CH_STR_REBOOT       = (CH_CMD_REBOOT, 0x01, 0x00, 0x01)

# Find the device
device = usb.core.find(idVendor=CH_USB_VENDOR_ID, idProduct=CH_USB_PRODUCT_ID)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-b', '--bootloader', help='Reboot to bootloader', action='store_true')
    parser.add_argument('-c', '--clear', help='Clear display', action='store_true')
    parser.add_argument('-r', '--random', help='Send random frame', action='store_true')
    args = parser.parse_args()

    if device is None:
        print("MCU not found")
        exit(0)

    if args.bootloader:
        print('rebooting to bootloader')
        bootloader()
    elif args.clear:
        clear_frame()
    elif args.random:
        send_random_frame()
    else:
        gui()
    
    print('done')

def bootloader():
    device.write(CH_USB_EP_OUT, CH_STR_REBOOT)

def clear_frame():
    device.write(CH_USB_EP_OUT, b'\1' + bytes(11 *4))
    device.write(CH_USB_EP_OUT, b'\2' + bytes(11 *4))

def send_random_frame():
    buf = b'\1' + os.urandom(11 *4) # 11 columns on the left side of the framebuffer, they take an uint32
    device.write(CH_USB_EP_OUT, buf)
    buf = b'\2' + os.urandom(11 *4) # 11 columns on the right side
    device.write(CH_USB_EP_OUT, buf)

def gui():
    import tkinter as t
    import struct
    BG_COLOR = "#2b2b2b"
    OFF_COLOR = "white"

    root = t.Tk()
    root.title("CH582 LED Badge")
    root.configure(bg=BG_COLOR)
    root.resizable(False, False)

    state = {'drag_color': None}
    var = t.StringVar(value="red")
    labels = []

    def get_target_color(widget_bg):
        selected = var.get()
        return OFF_COLOR if widget_bg == selected else selected

    def start_sweep(e):
        state['drag_color'] = get_target_color(e.widget['bg'])
        update_pixel(e.widget)

    def sweep(e):
        widget = root.winfo_containing(e.x_root, e.y_root)
        if widget in labels:
            update_pixel(widget)

    def update_pixel(w):
        if w['bg'] != state['drag_color']:
            w.config(bg=state['drag_color'])
            export_data()

    def change_active_colors():
        for l in labels:
            if l['bg'] != OFF_COLOR: l.config(bg=var.get())

    def clear_all():
        for l in labels: l.config(bg=OFF_COLOR)
        export_data()

    def export_data():
        active = [(i%44, i//44) for i, l in enumerate(labels) if l['bg'] != OFF_COLOR]
        fb = [0] * 22

        for i, l in enumerate(labels):
            if l['bg'] != OFF_COLOR:
                x, y = i % 44, i // 44

                idx = x // 2
                shift = y if (x % 2 == 0) else (y + 11)
                fb[idx] |= (1 << shift)

        device.write(CH_USB_EP_OUT, b'\1' + struct.pack('<11I', *fb[:11]))
        device.write(CH_USB_EP_OUT, b'\2' + struct.pack('<11I', *fb[11:]))


    grid_frame = t.Frame(root, bg=BG_COLOR)
    grid_frame.pack(padx=20, pady=20)

    for i in range(484):
        l = t.Label(grid_frame, width=2, height=1, bg=OFF_COLOR,
                    font=("Courier", 6), relief="flat", bd=0,
                    highlightthickness=1, highlightbackground=BG_COLOR, highlightcolor=BG_COLOR)

        l.grid(row=i//44, column=i%44)
        l.bind('<Button-1>', start_sweep)
        l.bind('<B1-Motion>', sweep)
        labels.append(l)

    # Status Bar
    bar = t.Frame(root, bg=BG_COLOR)
    bar.pack(fill="x", padx=20, pady=(0, 20))

    colors = ['red', 'green', 'blue', 'black']
    for c in colors:
        t.Radiobutton(bar, text=c.upper(), value=c, variable=var,
                      command=change_active_colors, indicatoron=False,
                      selectcolor=c, fg="black" if c != "black" else "white",
                      width=8, bd=0, highlightthickness=0
                      ).pack(side="left", padx=2)

    t.Button(bar, text="CLEAR", command=clear_all,
             bg="#555", fg="white", bd=0, width=10).pack(side="right")

    root.mainloop()


if __name__ == '__main__':
    main()
