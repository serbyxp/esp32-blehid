#!/usr/bin/env python3
"""
ESP32 Composite HID Client Example
Demonstrates control via both UART and WebSocket
"""

import argparse
import json
import logging
import time

import serial
import websocket

class HIDClient:
    """Base HID client interface"""
    
    def send_mouse(self, dx=0, dy=0, wheel=0, left=False, right=False, middle=False):
        """Send mouse movement and button state"""
        msg = {
            "type": "mouse",
            "dx": dx,
            "dy": dy,
            "wheel": wheel,
            "buttons": {
                "left": left,
                "right": right,
                "middle": middle
            }
        }
        self._send(msg)
    
    def send_keyboard(self, keys=None, modifiers=None):
        """Send keyboard key presses"""
        if keys is None:
            keys = []
        if modifiers is None:
            modifiers = {
                "left_control": False,
                "left_shift": False,
                "left_alt": False,
                "left_gui": False,
                "right_control": False,
                "right_shift": False,
                "right_alt": False,
                "right_gui": False
            }
        
        msg = {
            "type": "keyboard",
            "keys": keys,
            "modifiers": modifiers
        }
        self._send(msg)
    
    def send_control(self, cmd, **kwargs):
        """Send control command"""
        msg = {"type": "control", "cmd": cmd}
        msg.update(kwargs)
        self._send(msg)
    
    def _send(self, msg):
        """Override in subclass"""
        raise NotImplementedError


class UARTClient(HIDClient):
    """UART-based HID client"""
    
    def __init__(self, port, baudrate=115200):
        self.ser = serial.Serial(port, baudrate, timeout=1)
        time.sleep(0.1)  # Wait for connection
    
    def _send(self, msg):
        data = json.dumps(msg) + '\n'
        self.ser.write(data.encode())
        self.ser.flush()
    
    def read_response(self, timeout=1.0):
        """Read a response line"""
        self.ser.timeout = timeout
        line = self.ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                return {"raw": line}
        return None
    
    def close(self):
        self.ser.close()


class WebSocketClient(HIDClient):
    """WebSocket-based HID client"""
    
    def __init__(self, host, port=8765):
        self.ws = websocket.WebSocket()
        url = f"ws://{host}:{port}/ws"
        print(f"Connecting to {url}...")
        self.ws.connect(url)
        print("Connected!")
    
    def _send(self, msg):
        self.ws.send(json.dumps(msg))
    
    def receive(self, timeout=1.0):
        """Receive a message"""
        self.ws.settimeout(timeout)
        try:
            data = self.ws.recv()
            return json.loads(data)
        except websocket.WebSocketTimeoutException:
            return None
    
    def close(self):
        self.ws.close()


HID_KEYMAP_SHIFT = 0x80
HID_KEYMAP_LEFT_SHIFT = 0x02

# Fixed ASCII to HID keycode mapping copied from firmware
ASCII_TO_HID = [
    0, 0, 0, 0, 0, 0, 0, 0, 42, 43, 40, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    44, 158, 180, 160, 161, 162, 164, 52, 166, 167, 165, 174, 54, 45, 55, 56,
    39, 30, 31, 32, 33, 34, 35, 36, 37, 38, 179, 51, 182, 46, 183, 184,
    159, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
    147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 47, 49, 48, 163, 173,
    53, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 175, 177, 176, 181, 0,
]


def ascii_to_hid(char):
    """Convert a single character to HID keycode and modifiers."""
    if not char:
        return None

    ascii_code = ord(char)
    if ascii_code == 13:  # Carriage return uses the newline entry
        ascii_code = 10

    if ascii_code >= len(ASCII_TO_HID):
        return None

    entry = ASCII_TO_HID[ascii_code]
    if entry == 0:
        return None

    modifiers = 0
    keycode = entry
    if entry & HID_KEYMAP_SHIFT:
        modifiers |= HID_KEYMAP_LEFT_SHIFT
        keycode = entry & 0x7F

    if keycode == 0:
        return None

    return keycode, modifiers


def modifiers_mask_to_dict(mask):
    """Convert a modifier bitmask to the JSON structure expected by the firmware."""
    return {
        "left_control": False,
        "left_shift": bool(mask & HID_KEYMAP_LEFT_SHIFT),
        "left_alt": False,
        "left_gui": False,
        "right_control": False,
        "right_shift": False,
        "right_alt": False,
        "right_gui": False,
    }


def type_string(client, text, delay=0.05):
    """Type a string character by character"""
    for char in text:
        mapping = ascii_to_hid(char)
        if not mapping:
            logging.warning("Unsupported ASCII character: %r", char)
            continue

        keycode, modifiers = mapping
        modifiers_dict = modifiers_mask_to_dict(modifiers)

        client.send_keyboard(keys=[keycode], modifiers=modifiers_dict)
        time.sleep(delay)
        client.send_keyboard(keys=[], modifiers=modifiers_mask_to_dict(0))
        time.sleep(delay)


def demo_mouse_circle(client, radius=50, steps=36):
    """Move mouse in a circle"""
    import math
    print("Moving mouse in circle...")
    for i in range(steps):
        angle = 2 * math.pi * i / steps
        dx = int(radius * math.cos(angle) / 10)
        dy = int(radius * math.sin(angle) / 10)
        client.send_mouse(dx=dx, dy=dy)
        time.sleep(0.05)


def demo_mouse_clicks(client):
    """Demo mouse clicks"""
    print("Left click...")
    client.send_mouse(left=True)
    time.sleep(0.1)
    client.send_mouse(left=False)
    time.sleep(0.5)
    
    print("Right click...")
    client.send_mouse(right=True)
    time.sleep(0.1)
    client.send_mouse(right=False)


def demo_keyboard(client):
    """Demo keyboard typing"""
    print("Typing 'hello world'...")
    type_string(client, "hello world")
    time.sleep(0.5)

    print("Typing with Enter...")
    type_string(client, "test")
    enter_mapping = ascii_to_hid('\n')
    if enter_mapping:
        keycode, modifiers = enter_mapping
        client.send_keyboard(keys=[keycode], modifiers=modifiers_mask_to_dict(modifiers))
        time.sleep(0.05)
        client.send_keyboard(keys=[], modifiers=modifiers_mask_to_dict(0))


def main():
    logging.basicConfig(level=logging.INFO)

    parser = argparse.ArgumentParser(description='ESP32 HID Client Demo')
    parser.add_argument('--transport', choices=['uart', 'ws'], default='ws',
                       help='Transport type (uart or ws)')
    parser.add_argument('--port', default='/dev/ttyUSB0',
                       help='Serial port (for UART)')
    parser.add_argument('--host', default='192.168.4.1',
                       help='WebSocket host (for WS)')
    parser.add_argument('--ws-port', type=int, default=8765,
                       help='WebSocket port')
    parser.add_argument('--demo', choices=['mouse', 'keyboard', 'all'], default='all',
                       help='Demo to run')
    
    args = parser.parse_args()
    
    # Create client
    if args.transport == 'uart':
        print(f"Connecting via UART on {args.port}...")
        client = UARTClient(args.port)
    else:
        print(f"Connecting via WebSocket to {args.host}:{args.ws_port}...")
        client = WebSocketClient(args.host, args.ws_port)
    
    try:
        # Run demos
        if args.demo in ['mouse', 'all']:
            demo_mouse_circle(client)
            time.sleep(1)
            demo_mouse_clicks(client)
            time.sleep(1)
        
        if args.demo in ['keyboard', 'all']:
            demo_keyboard(client)
        
        print("\nControl commands:")
        print("Get WiFi status...")
        client.send_control("wifi_get")
        time.sleep(0.5)
        
        # For WebSocket, try to receive response
        if isinstance(client, WebSocketClient):
            response = client.receive(timeout=1.0)
            if response:
                print("Response:", json.dumps(response, indent=2))
        
        print("\nDemo complete!")
        
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    finally:
        client.close()


if __name__ == '__main__':
    main()
