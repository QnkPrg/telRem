#!/usr/bin/env python3
"""
Automated ESP32 audio streaming test
"""
import socket
import struct
import threading
import time
import sys
import numpy as np
import os
from collections import defaultdict

# Global variables for thread-safe video display
current_frame = None
current_frame_id = None
frame_condition = threading.Condition()  # Condition variable for frame availability
import cv2

# ESP32 Command definitions
class Commands:
    REQUEST_TALK = 0
    END_TALK = 1
    GRANT_TALK = 2
    DENY_TALK = 3
    TALK_ENDED = 4
    DOORBELL_RING = 5
    OPEN_DOOR = 6

# Packet type definitions (matching udp_stream.c)
class PacketTypes:
    AUDIO_PACKAGE = 0
    VIDEO_PACKAGE = 1

HEADER_FORMAT = '<BIQH'
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

# Video packet header format
VIDEO_HEADER_FORMAT = '<BIQHHH'
VIDEO_HEADER_SIZE = struct.calcsize(VIDEO_HEADER_FORMAT)

# Configuration
TCP_PORT = 12345
UDP_PORT = 12345
VIDEO_UDP_PORT = 12346
CHUNK_SIZE = 324
SAMPLE_RATE = 8000

def parse_video_header(data):
    """Parse video packet header and return header info and video data"""
    if len(data) < VIDEO_HEADER_SIZE:
        return None, None
        
    packet_type, frame_id, timestamp, length, packet_seq, total_packets = struct.unpack(VIDEO_HEADER_FORMAT, data[:VIDEO_HEADER_SIZE])
    
    video_data = data[VIDEO_HEADER_SIZE:VIDEO_HEADER_SIZE + length]
    
    header_info = {
        'type': packet_type,
        'frame_id': frame_id,
        'timestamp': timestamp,
        'length': length,
        'packet_seq': packet_seq,
        'total_packets': total_packets
    }
    
    return header_info, video_data

def parse_audio_header(data):
    """Parse UDP packet header and return header info and audio data"""
    
    packet_type, seq_num, timestamp, length = struct.unpack(HEADER_FORMAT, data[:HEADER_SIZE])

    audio_data = data[HEADER_SIZE:HEADER_SIZE + length]

    header_info = {
        'type': packet_type,
        'sequence': seq_num,
        'timestamp': timestamp,
        'length': length
    }

    return header_info, audio_data

def queue_video_frame_for_display(frame_data, frame_id):
    """Set current frame for main thread display (thread-safe)"""
    global current_frame, current_frame_id
    
    # First, validate the JPEG data
    if len(frame_data) < 10:
        return True
    
    # Check JPEG header and footer
    has_jpeg_header = frame_data[0] == 0xFF and frame_data[1] == 0xD8
    has_jpeg_footer = frame_data[-2] == 0xFF and frame_data[-1] == 0xD9
    
    if not has_jpeg_header:
        return True
    
    if not has_jpeg_footer:
        return True
    
    # Set current frame and notify main thread
    with frame_condition:
        current_frame = frame_data
        current_frame_id = frame_id
        frame_condition.notify()
        
    return True

def display_frame(frame_data, frame_id):
    """Display the current frame (called from main thread only)"""
        
    # Verify JPEG markers
    if not (frame_data[0] == 0xFF and frame_data[1] == 0xD8 and frame_data[-2] == 0xFF and frame_data[-1] == 0xD9):
        print(f"Frame {frame_id} has invalid JPEG markers, skipping")
        return False
    
    # Decode JPEG data
    frame_array = np.frombuffer(frame_data, dtype=np.uint8)
    frame = cv2.imdecode(frame_array, cv2.IMREAD_COLOR)
    
    if frame is not None:
        # Add frame info overlay
        cv2.putText(frame, f"Frame {frame_id}", (10, 30), 
                   cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
        
        # Display frame (safe in main thread)
        cv2.imshow('ESP32 Video Stream', frame)
        
        # Check for quit key
        key = cv2.waitKey(1) & 0xFF
        if key == ord('q') or key == 27:  # 'q' or ESC to quit
            print("User quit detected")
            return False
    else:
        print(f"Failed to decode frame {frame_id}")

    return True

def audio_processing_thread(udp_recv, udp_send, esp32_ip, stats, stop_event):
    """Dedicated thread for audio packet processing"""
    # Audio processing variables
    seq = None
    audio_store = {}
    
    while not stop_event.is_set():
        try:
            data, addr = udp_recv.recvfrom(CHUNK_SIZE + HEADER_SIZE + 50)
            
            # Parse the header and extract audio data
            header_info, audio_data = parse_audio_header(data)
            
            if header_info is None:
                continue
            
            if header_info['type'] == PacketTypes.AUDIO_PACKAGE:
                stats['audio_packets'] += 1
                
                # Check for missing sequences
                if seq is not None and seq + 1 != header_info['sequence']:
                    print(f"Missing audio sequence: expected {seq + 1}, got {header_info['sequence']}")
                seq = header_info['sequence']

                udp_send.sendto(data, (esp32_ip, UDP_PORT))

                # Prune old entries
                if len(audio_store) > 2000:
                    cutoff = seq - 1000
                    for k in list(audio_store.keys()):
                        if k < cutoff:
                            del audio_store[k]

        except socket.timeout:
            continue
        except Exception as e:
            if not stop_event.is_set():
                print(f"Audio receive error: {e}")

def video_processing_thread(video_udp_recv, stats, stop_event):
    """Simple video packet processing thread"""
    video_frames = defaultdict(dict)
    video_frame_info = {}
    
    while not stop_event.is_set():
        try:
            video_data, video_addr = video_udp_recv.recvfrom(65535)  # Max UDP packet size
            video_header, video_payload = parse_video_header(video_data)
            
            if video_header and video_header['type'] == PacketTypes.VIDEO_PACKAGE:
                stats['video_packets'] += 1
                frame_id = video_header['frame_id']
                packet_seq = video_header['packet_seq']
                total_packets = video_header['total_packets']
                
                # Track unique frames
                stats['unique_frames_seen'].add(frame_id)
                
                # Initialize frame info if needed
                if frame_id not in video_frame_info:
                    video_frame_info[frame_id] = {
                        'total_packets': total_packets,
                        'received_packets': set()
                    }
                
                # Store packet
                video_frames[frame_id][packet_seq] = video_payload
                video_frame_info[frame_id]['received_packets'].add(packet_seq)
                
                # Check if frame is complete
                if len(video_frame_info[frame_id]['received_packets']) == total_packets:
                    
                    # Assemble frame data quickly
                    frame_data = b''.join(video_frames[frame_id][i] for i in range(total_packets))
                    
                    # Queue frame for display in main thread
                    queue_video_frame_for_display(frame_data, frame_id)
                    
                    stats['completed_frames'] += 1
                    
                    # Clean up completed frame
                    del video_frames[frame_id]
                    del video_frame_info[frame_id]

        except socket.timeout:
            # Don't spam timeout messages
            continue
        except Exception as e:
            if not stop_event.is_set():
                print(f"Video receive error: {e}")
    
    print("Video processing thread stopping...")
    
    # Report any remaining incomplete frames
    if len(video_frames) > 0:
        print(f"{len(video_frames)} incomplete video frames at end")

def test_esp32_audio_video(esp32_ip: str):
    """Test ESP32 audio and video streaming with multi-threading"""
    print(f"Testing ESP32 audio and video streaming with {esp32_ip}")
    
    # Connect TCP
    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        tcp_sock.connect((esp32_ip, TCP_PORT))
        print("TCP connected")
    except Exception as e:
        print(f"TCP connection failed: {e}")
        return False
    
    # Setup UDP
    udp_send = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_recv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_recv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Setup video UDP
    video_udp_recv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    video_udp_recv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Increase UDP receive buffer size to handle burst of packets
    try:
        # Try to set a large receive buffer (4MB)
        video_udp_recv.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        actual_buffer = video_udp_recv.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    except Exception as e:
        print(f"Could not set large UDP buffer: {e}")
    
    try:
        udp_recv.bind(('0.0.0.0', UDP_PORT))
        video_udp_recv.bind(('0.0.0.0', VIDEO_UDP_PORT))
        print("UDP sockets ready (audio + video)")
    except Exception as e:
        print(f"UDP bind failed: {e}")
        tcp_sock.close()
        video_udp_recv.close()
        return False
    
    # Send talk request
    print("Requesting talk permission...")
    talk_request = struct.pack('<I', Commands.REQUEST_TALK)
    tcp_sock.send(talk_request)
    
    # Wait for response
    try:
        tcp_sock.settimeout(5.0)
        response_data = tcp_sock.recv(4)
        if len(response_data) == 4:
            response = struct.unpack('<I', response_data)[0]
            if response == Commands.GRANT_TALK:
                print("Talk permission GRANTED")
            else:
                print(f"Unexpected response: {response}")
                tcp_sock.close()
                udp_send.close()
                udp_recv.close()
                if video_udp_recv:
                    video_udp_recv.close()
                return False
        else:
            print("No response from ESP32")
            tcp_sock.close()
            udp_send.close()
            udp_recv.close()
            if video_udp_recv:
                video_udp_recv.close()
            return False
    except socket.timeout:
        print("Timeout waiting for talk permission")
        tcp_sock.close()
        udp_send.close()
        udp_recv.close()
        if video_udp_recv:
            video_udp_recv.close()
        return False
    
    # Shared statistics dictionary (thread-safe for simple counters)
    stats = {
        'audio_packets': 0,
        'video_packets': 0,
        'completed_frames': 0,
        'unique_frames_seen': set(),  # Track unique frame IDs
    }
    
    # Threading control
    stop_event = threading.Event()
    threads = []
    
    # Set UDP receive timeout for threads
    udp_recv.settimeout(0.1)
    if video_udp_recv:
        video_udp_recv.settimeout(0.1)
    
    # Start audio processing thread
    audio_thread = threading.Thread(
        target=audio_processing_thread,
        args=(udp_recv, udp_send, esp32_ip, stats, stop_event),
        name="AudioProcessor"
    )
    audio_thread.daemon = True
    audio_thread.start()
    threads.append(audio_thread)
    print("Audio processing thread started")
    
    # Start video processing thread
    video_thread = threading.Thread(
        target=video_processing_thread,
        args=(video_udp_recv, stats, stop_event),
        name="VideoProcessor"
    )
    video_thread.daemon = True
    video_thread.start()
    threads.append(video_thread)
    print("Video processing thread started")

    # Start time for test duration
    start_time = time.time()
    
    # Main thread monitors for duration, early exit, and displays video frames
    try:
        while not stop_event.is_set():
            # Handle video frame display in main thread (thread-safe)
            with frame_condition:
                frame_condition.wait(timeout=0.1)  # 100ms timeout to check stop_event
                global current_frame, current_frame_id
                if current_frame and current_frame_id:
                    if not display_frame(current_frame, current_frame_id):
                        # User pressed 'q' or ESC to quit
                        print("Video display stopped by user")
                        stop_event.set()
                        break
                current_frame = None
                current_frame_id = None

            # Print periodic stats
            elapsed = time.time() - start_time
            if int(elapsed) % 5 == 0 and elapsed - int(elapsed) < 0.1:  # Every 5 seconds
                print(f"{elapsed:.1f}s - Audio: {stats['audio_packets']}, Video: {stats['video_packets']}, Frames: {stats['completed_frames']}")
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        stop_event.set()
    
    # Signal threads to stop and wait for completion
    stop_event.set()
    print("\nStopping processing threads...")
    
    for thread in threads:
        thread.join(timeout=1.0)
        if thread.is_alive():
            print(f"Thread {thread.name} did not stop gracefully")
    
    # End talk session
    print("Ending talk session...")
    end_talk = struct.pack('<I', Commands.END_TALK)
    tcp_sock.send(end_talk)

    end_time= time.time()
    elapsed_time = end_time - start_time
    
    # Cleanup video window if it was opened
    cv2.destroyAllWindows()
    print("Video display window closed")
    
    # Results
    print(f"\nTest Results:")
    print(f"  Audio packets: {stats['audio_packets']}")
    print(f"  Video packets: {stats['video_packets']}")
    print(f"  Completed video frames: {stats['completed_frames']}")
    print(f"  Unique frames seen: {len(stats['unique_frames_seen'])}")
    
    # Frame completion analysis
    if stats['unique_frames_seen']:
        frame_ids = sorted(list(stats['unique_frames_seen']))
        unique_frame_count = len(frame_ids)
        incomplete_frames = stats['completed_frames'] - unique_frame_count

        print(f"  Frame completion rate: {stats['completed_frames']}/{unique_frame_count} ({100*stats['completed_frames']/unique_frame_count:.1f}%)")
        if incomplete_frames > 0:
            print(f"  Incomplete frames: {incomplete_frames}")
    
    print(f"  Audio rate: {stats['audio_packets']/elapsed_time:.1f} packets/sec")
    print(f"  Video rate: {stats['video_packets']/elapsed_time:.1f} packets/sec")
    print(f"  Video frame rate: {stats['completed_frames']/elapsed_time:.1f} frames/sec")

    expected_audio_rate = (SAMPLE_RATE * 2 / CHUNK_SIZE)
    print(f"  Expected audio rate: {expected_audio_rate:.1f} packets/sec")
    
    # Packet analysis
    if stats['audio_packets'] > 0:
        audio_performance = 100 * stats['audio_packets'] / (expected_audio_rate * elapsed_time)   # Percentage of expected performance
        print(f"\nPerformance Analysis:")
        print(f"  ESP32 audio performance: {audio_performance:.1f}% of expected rate")
    
    if stats['audio_packets'] == 0:
        print("No audio packets received - check connection")
    elif stats['audio_packets'] < expected_audio_rate * 2:
        print("Very low audio rate - ESP32 likely running too slow")
    elif stats['audio_packets'] < expected_audio_rate * 4:
        print("Low packet rate - ESP32 may be struggling")
    else:
        print("ESP32 appears to be processing audio at good speed")
    
    # Cleanup
    tcp_sock.close()
    udp_send.close()
    udp_recv.close()
    if video_udp_recv:
        video_udp_recv.close()
    
    # Report video results
    if stats['completed_frames'] > 0:
        print(f"Displayed {stats['completed_frames']} video frames in real-time using separate threads")

    return stats['audio_packets'] > 0

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python auto_esp32_test.py <esp32_ip>")
        print("  Press 'q' or ESC in video window to stop")
        sys.exit(1)
    
    esp32_ip = sys.argv[1]
    
    success = test_esp32_audio_video(esp32_ip)
    if success:
        print(f"\nAudio and video streaming test completed successfully")
    else:
        print(f"\nAudio and video streaming test failed")
