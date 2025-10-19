#!/usr/bin/env python3
"""
ESP32 Composite HID Client Example
Demonstrates control via both UART and WebSocket
"""

import json
import time
import argparse
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


# HID Key Codes
HID_KEYS = {
    'a': 0x04, 'b': 0x05, 'c': 0x06, 'd': 0x07,
    'e': 0x08, 'f': 0x09, 'g': 0x0A, 'h': 0x0B,
    'i': 0x0C, 'j': 0x0D, 'k': 0x0E, 'l': 0x0F,
    'm': 0x10, 'n': 0x11, 'o': 0x12, 'p': 0x13,
    'q': 0x14, 'r': 0x15, 's': 0x16, 't': 0x17,
    'u': 0x18, 'v': 0x19, 'w': 0x1A, 'x': 0x1B,
    'y': 0x1C, 'z': 0x1D,
    '1': 0x1E, '2': 0x1F, '3': 0x20, '4': 0x21,
    '5': 0x22, '6': 0x23, '7': 0x24, '8': 0x25,
    '9': 0x26, '0': 0x27,
    'enter': 0x28, 'esc': 0x29, 'backspace': 0x2A,
    'tab': 0x2B, 'space': 0x2C,
}


def type_string(client, text, delay=0.05):
    """Type a string character by character"""
    for char in text.lower():
        if char in HID_KEYS:
            # Press key
            client.send_keyboard(keys=[HID_KEYS[char]])
            time.sleep(delay)
            # Release key
            client.send_keyboard(keys=[])
            time.sleep(delay)
        elif char == ' ':
            client.send_keyboard(keys=[HID_KEYS['space']])
            time.sleep(delay)
            client.send_keyboard(keys=[])
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
    client.send_keyboard(keys=[HID_KEYS['enter']])
    time.sleep(0.05)
    client.send_keyboard(keys=[])


def main():
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
