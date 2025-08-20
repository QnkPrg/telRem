# Audio and Video Packet Formats

This document describes the UDP packet formats used for audio and video streaming in the ESP32 multimedia system.

## Audio Packet Format (UDP Stream)

Audio packets are transmitted through the UDP stream component with the following structure:

### Header Structure (15 bytes)
```
Offset | Size | Field       | Description
-------|------|-------------|------------------------------------------
0      | 1    | Type        | Package type (AUDIO_PACKAGE = 0)
1      | 4    | Sequence    | Packet sequence number (incremental)
5      | 8    | Timestamp   | Timestamp in milliseconds since EPOCH
13     | 2    | Length      | Audio data payload size in bytes
15     | N    | Data        | Audio data payload
```

### Field Details
- **Type**: Identifies the packet as an audio packet (value: 0)
- **Sequence**: Incremental counter for packet ordering and loss detection
- **Timestamp**: 64-bit timestamp for audio synchronization
- **Length**: Size of the audio data portion (excluding header)
- **Data**: Raw audio data payload

### Packet Types
- `AUDIO_PACKAGE = 0` - Standard audio data packet

## Video Packet Format (Video Stream)

Video frames are fragmented into multiple UDP packets due to size constraints. Each packet contains part of a JPEG frame.

### Header Structure (19 bytes)
```
Offset | Size | Field         | Description
-------|------|---------------|------------------------------------------
0      | 1    | Type          | Package type (VIDEO_PACKAGE = 1)
1      | 4    | Frame ID      | Unique identifier for the frame
5      | 8    | Timestamp     | Frame timestamp in milliseconds since EPOCH
13     | 2    | Length        | Video data payload size in bytes
15     | 2    | Packet Seq    | Packet sequence within the frame (0-based)
17     | 2    | Total Packets | Total number of packets in the complete frame
19     | N    | Data          | JPEG frame fragment data
```

### Field Details
- **Type**: Identifies the packet as a video packet (value: 1)
- **Frame ID**: Unique identifier for each video frame (incremental)
- **Timestamp**: 64-bit timestamp for audio-video synchronization
- **Length**: Size of the video data fragment (excluding header)
- **Packet Seq**: Position of this packet within the frame (0 to Total Packets - 1)
- **Total Packets**: Total number of packets needed to reconstruct the complete frame
- **Data**: JPEG frame fragment

### Frame Reconstruction
1. Collect all packets with the same Frame ID
2. Sort packets by Packet Seq (0 to Total Packets - 1)
3. Concatenate data payloads in sequence order
4. Verify all packets received (Packet Seq 0 to Total Packets - 1)
5. Decode the complete JPEG frame

## Transmission Parameters

### Audio Stream
- **Port**: 12345
- **Packet Size**: 339 B (15 Header + 324 Data)
- **Audio Format**: 8 Khz 16 bit PCM Mono

### Video Stream
- **Port**: 12346
- **Max Packet Size**: 1400 bytes (MTU-safe)
- **Max Data per Packet**: 1381 bytes (1400 - 19 header bytes)
- **Frame Rate**: 20 FPS
- **Resolution**: VGA (640x480)
- **Format**: JPEG with quality setting 20

## Synchronization

Both audio and video packets include 8-byte timestamps for precise synchronization:
- **Audio**: Timestamp of audio sample capture
- **Video**: Timestamp of frame capture
- **Resolution**: Milliseconds since EPOCH
- **Usage**: Client can align audio and video streams using these timestamps

## Error Handling

### Packet Loss Detection
- **Audio**: Packet did not arrive within 20ms + 5ms 
- **Video**: Full frame did not arrive within 50ms + 5ms

### Recovery Strategies
- **Audio**: Insert silence for missing packets or use interpolation
- **Video**: Skip incomplete frames
