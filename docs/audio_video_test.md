

# ESP32 Audio & Video Streaming Test Script

This script tests real-time audio and video streaming from an ESP32 device. It connects to the ESP32, requests streaming permission, and processes incoming audio and video data using multi-threading for performance and reliability.

## Overview
- Connects to the ESP32 via TCP to request talk permission.
- Receives audio and video packets over UDP.
- Displays video frames in real time using OpenCV.
- Monitors and reports streaming statistics, including packet rates and frame completion.
- Allows early termination by pressing 'q' or ESC in the video window.

## Features
- Multi-threaded audio and video packet processing.
- Real-time video display (OpenCV required).
- Performance metrics: audio/video packet rates, frame completion, unique frames.
- User interaction: early exit via keyboard, graceful interruption handling.

## Usage
Run the script with Python 3:

```bash
python3 audio_video_test.py <esp32_ip>
```

- `<esp32_ip>`: IP address of the ESP32 device.

## Requirements
- Python 3
- `numpy` (for video frame decoding)
- `opencv-python` (for real-time video display)

## Typical Workflow
1. Connect your computer to the same network as the ESP32.
2. Run the script, providing the ESP32's IP address.
3. The script requests permission to stream, then starts receiving and displaying video frames and audio packets.
4. Statistics and performance analysis are printed during and after the test.

## Troubleshooting
- If no audio / video packets are received, check the ESP32's network connection.

## Notes
- The script is intended for quick testing and diagnostics of ESP32 streaming capabilities.
- Video display requires OpenCV.
- The script can be interrupted at any time with Ctrl+C or by pressing 'q'/ESC in the video window.

---
For more details, see the script source code or run with the required arguments for usage instructions.
