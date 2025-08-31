# Simple ESP32 WiFi Provisioning Script

The script (`simple_provisioning.py`) provides a simple way to provision an ESP32 device with WiFi credentials using HTTP requests. It is designed to work with ESP32 firmware that exposes `/scan` and `/config` endpoints for WiFi management.

## Features
- **Scan for WiFi Networks:**
  - Queries the ESP32 for available WiFi networks and displays them sorted by signal strength.
- **Interactive Provisioning:**
  - Guides the user through selecting a network and entering credentials.
  - Supports manual SSID and password entry if scanning is not desired or fails.

## Usage
Run the script with Python 3:

```bash
python3 simple_provisioning.py
```

### Interactive Mode
- The script will prompt you to scan for networks and guide you through the provisioning process.

### Command-Line Arguments
- `python3 simple_provisioning.py <IP> <SSID> <PASSWORD>`: Provision directly with provided credentials.
- `python3 simple_provisioning.py scan <IP>`: Only scan for available networks.

### Help
- `python3 simple_provisioning.py --help`

## Example Workflow
1. Connect your computer to the ESP32's WiFi AP.
2. Run the script and follow the prompts to scan for networks and enter credentials.
3. The script sends the credentials to the ESP32 and reports success or failure.

## Troubleshooting
- If you cannot connect, ensure:
  - The ESP32 is powered on and in provisioning mode.
  - You are connected to the ESP32's WiFi AP.
  - The IP address is correct (default: `192.168.4.1`).

---
For more details, see the script source code or run with `--help` for usage instructions.
