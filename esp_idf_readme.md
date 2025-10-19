# ESP32 Composite HID Device (ESP-IDF v5.5.1)

A Bluetooth Low Energy HID device that acts as both a mouse and keyboard, controllable via UART and WebSocket. Built with ESP-IDF v5.5.1 using NimBLE stack.

## Features

- **Composite HID**: Mouse + Keyboard in single BLE advertisement
- **Dual Transport**: UART and WiFi WebSocket control
- **Bonding Support**: Persistent pairing with NVS storage
- **WiFi Management**: AP and STA modes with web configuration
- **iOS Compatible**: Proper mouse appearance for iOS pairing

## Hardware Requirements

- ESP32 (tested on ESP32-WROOM-32)
- USB cable for programming and UART communication

## Software Requirements

- ESP-IDF v5.5.1
- Python 3.7+ with esptool

## Building and Flashing

```bash
# Set up ESP-IDF environment
. $HOME/esp/esp-idf/export.sh

# Configure (optional)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Default Configuration

- **Device Name**: Composite HID
- **AP SSID**: ESP32-HID
- **AP Password**: composite
- **AP IP**: 192.168.4.1
- **WebSocket Port**: 8765
- **UART**: UART0 @ 115200 baud

## Usage

### UART Control

Send JSON commands over UART (115200 baud, 8N1):

**Mouse Movement:**
```json
{"type":"mouse","dx":10,"dy":-5,"wheel":0,"buttons":{"left":false,"right":false,"middle":false}}
```

**Keyboard Input:**
```json
{"type":"keyboard","keys":[0x04,0x05],"modifiers":{"left_shift":true,"left_control":false}}
```

**Control Commands:**
```json
{"type":"control","cmd":"force_adv"}
{"type":"control","cmd":"quiet"}
{"type":"control","cmd":"forget"}
{"type":"control","cmd":"wifi_get"}
{"type":"control","cmd":"wifi_set","ssid":"MyWiFi","psk":"password","apply":true}
```

### WebSocket Control

Connect to `ws://<ESP32_IP>:8765/ws` and send the same JSON format.

**Example with Python:**
```python
import websocket
import json

ws = websocket.WebSocket()
ws.connect("ws://192.168.4.1:8765/ws")

# Move mouse
ws.send(json.dumps({"type":"mouse","dx":50,"dy":0}))

# Press 'A' key (HID code 0x04)
ws.send(json.dumps({"type":"keyboard","keys":[0x04]}))
ws.send(json.dumps({"type":"keyboard","keys":[]}))  # Release

ws.close()
```

**Example with JavaScript:**
```javascript
const ws = new WebSocket('ws://192.168.4.1:8765/ws');

ws.onopen = () => {
    // Move mouse right 100 pixels
    ws.send(JSON.stringify({
        type: 'mouse',
        dx: 100,
        dy: 0,
        buttons: {left: false, right: false, middle: false}
    }));
    
    // Type "Hello"
    const keys = [0x0B, 0x08, 0x0F, 0x0F, 0x12]; // H-E-L-L-O
    keys.forEach(key => {
        ws.send(JSON.stringify({type: 'keyboard', keys: [key]}));
        setTimeout(() => {
            ws.send(JSON.stringify({type: 'keyboard', keys: []}));
        }, 50);
    });
};
```

## HID Key Codes

Common keyboard HID usage codes:
- `0x04`: A
- `0x05`: B
- `0x06`: C
- ...
- `0x1E`: 1
- `0x1F`: 2
- `0x28`: Enter
- `0x2C`: Space

Modifier bits (in `modifiers` byte):
- `0x01`: Left Control
- `0x02`: Left Shift
- `0x04`: Left Alt
- `0x08`: Left GUI (Win/Cmd)
- `0x10`: Right Control
- `0x20`: Right Shift
- `0x40`: Right Alt
- `0x80`: Right GUI

## WiFi Configuration

### Via WebSocket/UART:
```json
{"type":"control","cmd":"wifi_set","ssid":"YourSSID","psk":"YourPassword","apply":true}
```

### Manual (via menuconfig):
```bash
idf.py menuconfig
# Navigate to Component config -> WiFi
```

## Pairing

1. Device advertises on startup for 30 seconds
2. Pair from your host device (Windows/Mac/iOS/Android)
3. Bonding information is stored in NVS
4. After bonding, device won't advertise unless:
   - Unpaired/disconnected and input is detected
   - `force_adv` command is sent
   - `forget` command clears bonds

### Force Advertising:
```json
{"type":"control","cmd":"force_adv"}
```

### Clear Bonding:
```json
{"type":"control","cmd":"forget"}
```

## Architecture

```
┌─────────────────────────────────────────────┐
│              Application Layer               │
│  (main.c - Input aggregation & control)     │
└──────┬──────────────────────────┬───────────┘
       │                          │
   ┌───▼────┐                ┌────▼─────┐
   │  UART  │                │WebSocket │
   │Transport│               │Transport │
   └───┬────┘                └────┬─────┘
       │                          │
       └──────────┬───────────────┘
                  │
         ┌────────▼────────┐
         │   HID Device    │
         │  (hid_device.c) │
         └────────┬────────┘
                  │
         ┌────────▼────────┐
         │   BLE HID       │
         │  (ble_hid.c)    │
         │   NimBLE Stack  │
         └─────────────────┘
```

## Troubleshooting

### Device won't advertise
- Check if already bonded: send `{"type":"control","cmd":"forget"}`
- Force advertising: send `{"type":"control","cmd":"force_adv"}`

### Can't connect to WebSocket
- Check WiFi mode: send `{"type":"control","cmd":"wifi_get"}` via UART
- Verify IP address in logs
- Ensure firewall allows port 8765

### UART not responding
- Check baud rate (115200)
- Verify correct COM port
- Try different USB cable

### iOS won't pair
- Device must advertise as mouse (appearance 0x03C2) - already configured
- Ensure "Assistive Touch" is enabled in iOS settings
- Some iOS versions require screen to be unlocked during pairing

## Customization

### Change Device Name:
Edit `main/main.c`:
```c
g_device = hid_device_create("Your Device Name");
```

### Change WiFi AP Credentials:
Edit `main/main.c`:
```c
#define DEFAULT_AP_SSID "Your-SSID"
#define DEFAULT_AP_PASS "your-password"
```

### Change WebSocket Port:
Edit `main/transport_websocket.h`:
```c
#define DEFAULT_WS_PORT 8080
```

### Adjust Notification Rate:
Edit `main/main.c`:
```c
#define NOTIFY_INTERVAL_MS 50  // Change to desired ms
```

## License

GPLv3 - See original MicroPython implementation for attribution.

## Credits

Based on the MicroPython HID library by H. Groefsema, ported to ESP-IDF v5.5.1 with NimBLE and enhanced transport layers.
