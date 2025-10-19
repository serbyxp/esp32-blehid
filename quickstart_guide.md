# Quick Start Guide

## 1. Hardware Setup

Connect your ESP32 to your computer via USB.

## 2. Install ESP-IDF v5.5.1

```bash
# Clone ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git

# Install prerequisites
cd ~/esp/esp-idf
./install.sh

# Set up environment (add to ~/.bashrc for permanent use)
. $HOME/esp/esp-idf/export.sh
```

## 3. Build and Flash

```bash
# Navigate to project directory
cd /path/to/composite_hid

# Configure (optional - uses defaults)
idf.py menuconfig

# Build
idf.py build

# Find your serial port
ls /dev/ttyUSB*  # Linux
ls /dev/cu.*     # macOS

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

Press `Ctrl+]` to exit the monitor.

## 4. Connect via WiFi

The device starts in AP mode by default:

- **SSID**: ESP32-HID
- **Password**: composite
- **IP**: 192.168.4.1

Connect your phone/computer to this network.

## 5. Test with Web Interface

Open `examples/web_control.html` in a browser:

1. Update WebSocket URL if needed: `ws://192.168.4.1:8765/ws`
2. Click "Connect"
3. Try moving the mouse on the touchpad
4. Type text and click "Send Text"

## 6. Test with Python

```bash
# Install dependencies
pip install websocket-client pyserial

# Run demo
python3 examples/python_client.py --transport ws --host 192.168.4.1
```

## 7. Pair with Your Device

### Windows
1. Settings → Bluetooth & devices → Add device
2. Select "Mouse, keyboard & pen"
3. Choose "Composite HID"
4. Device will appear as a mouse

### macOS
1. System Preferences → Bluetooth
2. Wait for "Composite HID" to appear
3. Click "Connect"

### iOS/iPadOS
1. Settings → Accessibility → Touch → Assistive Touch (Enable)
2. Settings → Bluetooth
3. Connect to "Composite HID"
4. Device appears under "Pointer Devices"

### Android
1. Settings → Connected devices → Pair new device
2. Select "Composite HID"

## 8. Configure Your WiFi Network

**Via Web Interface:**
1. Open web control interface
2. Enter your WiFi SSID and password
3. Click "Connect to WiFi"
4. Device will restart and connect to your network
5. Note the new IP address in logs

**Via Python:**
```python
from examples.python_client import WebSocketClient

client = WebSocketClient("192.168.4.1")
client.send_control("wifi_set", ssid="YourSSID", psk="YourPassword", apply=True)
```

**Via UART:**
```bash
# Using screen
screen /dev/ttyUSB0 115200

# Send JSON
{"type":"control","cmd":"wifi_set","ssid":"YourSSID","psk":"YourPassword","apply":true}
```

## Troubleshooting

### Can't connect to AP
- Verify password: "composite"
- Check your device supports 2.4GHz WiFi
- Try forgetting and reconnecting

### Device won't advertise
```python
# Force advertising
client.send_control("force_adv")
```

### Clear pairing and start fresh
```python
# Forget all bonds
client.send_control("forget")
```

### Check device status
```python
# Get WiFi info
client.send_control("wifi_get")
```

### Serial monitor shows errors
- Check baud rate is 115200
- Verify ESP32 is properly connected
- Try different USB cable/port

## Next Steps

- See `README.md` for detailed documentation
- Explore `examples/python_client.py` for automation ideas
- Customize device name in `main/main.c`
- Adjust WiFi credentials in code for default STA mode

## Common Use Cases

### Remote Control
Control your computer from across the room using the web interface.

### Automation
Use Python scripts to automate mouse/keyboard actions.

### Accessibility
Create custom input devices for assistive technology.

### Gaming
Map physical buttons to keyboard/mouse for game controllers.

### Testing
Automate UI testing with programmatic input.
