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
    args = parser.parse_args()

    if device is None:
        print("MCU not found")
        exit(0)

    if args.bootloader:
        print('rebooting to bootloader')
        bootloader()
    elif args.clear:
        clear_frame()
    else:
        send_random_frame()
    
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


if __name__ == '__main__':
    main()
