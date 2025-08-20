#!/usr/bin/env python3
"""
Simple ESP32 WiFi Provisioning Client
This demonstrates how easy it is to provision ESP32 with our custom HTTP-based system.
Perfect for Android app integration or Python scripts.
"""

import requests
import json
import time
import sys

def scan_wifi_networks(esp32_ip):
    """
    Scan for available WiFi networks using ESP32
    """
    try:
        print("📡 Scanning for available WiFi networks...")
        response = requests.get(f"http://{esp32_ip}/scan", timeout=15)  # Scan can take a while
        
        if response.status_code == 200:
            scan_result = response.json()
            networks = scan_result.get('networks', [])
            count = scan_result.get('count', 0)
            
            if count == 0:
                print("   ⚠️  No WiFi networks found")
                return []
            
            print(f"   ✅ Found {count} WiFi networks:")
            print("   " + "=" * 70)
            print("   {:<25} {:<8} {:<8} {:<15}".format("SSID", "Signal", "Channel", "Security"))
            print("   " + "-" * 70)
            
            # Sort by signal strength (RSSI)
            networks.sort(key=lambda x: x.get('rssi', -100), reverse=True)
            
            for i, network in enumerate(networks, 1):
                ssid = network.get('ssid', 'Unknown')
                rssi = network.get('rssi', -100)
                channel = network.get('channel', 0)
                security = network.get('security', 'Unknown')
                
                # Convert RSSI to signal bars
                if rssi >= -50:
                    signal = "-----"
                elif rssi >= -60:
                    signal = "---- "
                elif rssi >= -70:
                    signal = "---  "
                elif rssi >= -80:
                    signal = "--   "
                else:
                    signal = "-    "
                
                print(f"   {i:2}. {ssid:<22} {signal} Ch{channel:<2} {security}")
            
            print("   " + "=" * 70)
            return networks
            
        else:
            print(f"   ❌ Scan failed (HTTP {response.status_code})")
            return []
            
    except requests.exceptions.RequestException as e:
        print(f"   ❌ Scan failed: {e}")
        return []

def provision_esp32(esp32_ip, ssid, password):
    """
    Provision ESP32 with WiFi credentials using simple HTTP requests
    """
    base_url = f"http://{esp32_ip}"
    
    print(f"🚀 Starting provisioning for ESP32 at {esp32_ip}")
    print(f"📡 WiFi SSID: {ssid}")
    print(f"🔐 WiFi Password: {'*' * len(password) if password else '(open network)'}")
    print()
    
    try:
        # Step 1: Check if ESP32 is reachable
        print("1️⃣  Checking ESP32 connectivity...")
        response = requests.get(f"{base_url}/info", timeout=5)
        if response.status_code == 200:
            info = response.json()
            print(f"   ✅ Connected to {info.get('device', 'ESP32')} v{info.get('version', 'unknown')}")
        else:
            print(f"   ⚠️  ESP32 responded with status {response.status_code}")
        
        # Step 2: Check current status
        print("\n2️⃣  Checking current WiFi status...")
        response = requests.get(f"{base_url}/status", timeout=5)
        if response.status_code == 200:
            status = response.json()
            print(f"   📊 Status: {status.get('status', 'unknown')}")
            if status.get('ssid'):
                print(f"   📡 Current SSID: {status['ssid']}")
        
        # Step 3: Send WiFi credentials
        print("\n3️⃣  Sending WiFi credentials...")
        credentials = {
            "ssid": ssid,
            "password": password
        }
        
        response = requests.post(
            f"{base_url}/config", 
            json=credentials,
            headers={'Content-Type': 'application/json'},
            timeout=10
        )
        
        if response.status_code == 200:
            result = response.json()
            if result.get('success'):
                print("   ✅ Credentials sent successfully!")
            else:
                print("   ❌ ESP32 rejected the credentials")
                return False
        else:
            print(f"   ❌ Failed to send credentials (HTTP {response.status_code})")
            return False
        
        # Step 4: Monitor connection status  
        print("\n4️⃣  Waiting for ESP32 to connect to WiFi...")
        for attempt in range(60):  # Wait up to 60 seconds (gives more time for success notification)
            time.sleep(1)
            try:
                response = requests.get(f"{base_url}/status", timeout=5)  # Increased timeout
                if response.status_code == 200:
                    status = response.json()
                    current_status = status.get('status')
                    attempts = status.get('connection_attempts', 0)
                    max_attempts = status.get('max_attempts', 5)
                    
                    if current_status == 'connected':
                        print(f"   🎉 SUCCESS! ESP32 connected to {status.get('ssid', ssid)}")
                        print(f"   📱 Note: ESP32 AP will stop in ~10 seconds (this is normal)")
                        print(f"   🌐 ESP32 is now on your WiFi network")
                        return True
                    elif current_status == 'failed':
                        print(f"   ❌ CONNECTION FAILED after {attempts} attempts!")
                        error_msg = status.get('error', 'Unknown error')
                        print(f"   💡 Error: {error_msg}")
                        print(f"   🔍 Please check:")
                        print(f"      • SSID is correct: '{ssid}'")
                        print(f"      • Password is correct")
                        print(f"      • WiFi network is available")
                        print(f"      • Network uses supported security (WPA/WPA2)")
                        return False
                    elif current_status == 'connecting':
                        print(f"   🔄 Connecting... (attempt {attempts}/{max_attempts}) - {attempt + 1}/60")
                    else:
                        print(f"   ⏳ Status: {current_status} ({attempt + 1}/60)")
                else:
                    # ESP32 might have switched networks and is no longer reachable on this IP
                    print(f"   📡 ESP32 may have switched to target network (attempt {attempt + 1}/60)")
                    
            except requests.exceptions.RequestException:
                # This might happen when ESP32 switches from APSTA to STA mode after success
                if attempt > 30:  # Only mention this after giving enough time for success notification
                    print(f"   🔄 ESP32 may have switched to STA mode (attempt {attempt + 1}/60)")
                else:
                    print(f"   🔄 Waiting for response... (attempt {attempt + 1}/60)")
        
        print("   ⏱️  Timeout waiting for connection confirmation")
        print("   💡 ESP32 may have connected but is now on the target network")
        return None  # Unknown status
        
    except requests.exceptions.ConnectionError:
        print(f"❌ Cannot connect to ESP32 at {esp32_ip}")
        print("   Make sure:")
        print("   • ESP32 is powered on and in provisioning mode")
        print("   • You're connected to the ESP32's WiFi AP")
        print("   • The IP address is correct (usually 192.168.4.1)")
        return False
        
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        return False

def main():
    print("=" * 60)
    print("🔧 Simple ESP32 WiFi Provisioning")
    print("=" * 60)
    print()
    
    # Check for scan-only mode
    if len(sys.argv) == 3 and sys.argv[1] == "scan":
        esp32_ip = sys.argv[2]
        print(f"🔍 Scanning WiFi networks via ESP32 at {esp32_ip}")
        print()
        networks = scan_wifi_networks(esp32_ip)
        if networks:
            print(f"\n💡 To provision, run:")
            print(f"   python3 {sys.argv[0]} {esp32_ip} <SSID> <PASSWORD>")
        return
    
    if len(sys.argv) == 4:
        # Command line arguments
        esp32_ip = sys.argv[1]
        ssid = sys.argv[2]
        password = sys.argv[3]
    elif len(sys.argv) == 2 and sys.argv[1] in ['-h', '--help', 'help']:
        print("Usage:")
        print(f"  {sys.argv[0]}                              # Interactive mode")
        print(f"  {sys.argv[0]} <IP> <SSID> <PASSWORD>       # Direct provisioning")
        print(f"  {sys.argv[0]} scan <IP>                    # Scan networks only")
        print()
        print("Examples:")
        print(f"  {sys.argv[0]}                              # Interactive with WiFi scan")
        print(f"  {sys.argv[0]} 192.168.4.1 MyWiFi mypass    # Quick provision")
        print(f"  {sys.argv[0]} scan 192.168.4.1             # Just scan networks")
        return
    elif len(sys.argv) == 4:
        # Command line arguments
        esp32_ip = sys.argv[1]
        ssid = sys.argv[2]
        password = sys.argv[3]
    else:
        # Interactive mode
        print("📋 Enter provisioning details:")
        esp32_ip = input("ESP32 IP address (default: 192.168.4.1): ").strip()
        if not esp32_ip:
            esp32_ip = "192.168.4.1"
        
        print()
        
        # Ask if user wants to scan for networks
        scan_choice = input("🔍 Would you like to scan for available WiFi networks? (y/n, default: y): ").strip().lower()
        if scan_choice != 'n':
            networks = scan_wifi_networks(esp32_ip)
            
            if networks:
                print()
                while True:
                    choice = input("📋 Select network by number, or type SSID manually (or 'q' to quit): ").strip()
                    
                    if choice.lower() == 'q':
                        print("👋 Provisioning cancelled")
                        return
                    
                    # Check if user entered a number to select from scan results
                    try:
                        network_num = int(choice)
                        if 1 <= network_num <= len(networks):
                            selected_network = networks[network_num - 1]
                            ssid = selected_network['ssid']
                            security = selected_network['security']
                            
                            print(f"   ✅ Selected: \'{ssid}\' ({security})")
                            
                            # Ask for password if network is secured
                            if security.lower() != 'open':
                                password = input(f"🔐 Enter password for '{ssid}': ").strip()
                            else:
                                password = ""
                                print("   🔓 Open network - no password required")
                            break
                        else:
                            print(f"   ❌ Invalid selection. Choose 1-{len(networks)}")
                            continue
                    except ValueError:
                        # User typed an SSID manually
                        ssid = choice
                        if ssid:
                            print(f"   ✅ Manual SSID: {ssid}")
                            password = input(f"🔐 Enter password for '{ssid}' (leave empty for open network): ").strip()
                            break
                        else:
                            print("   ❌ SSID cannot be empty")
                            continue
            else:
                # Scan failed or no networks found, fall back to manual entry
                print("📝 Manual WiFi configuration:")
                ssid = input("WiFi SSID: ").strip()
                if not ssid:
                    print("❌ SSID cannot be empty")
                    return
                password = input("WiFi Password (leave empty for open network): ").strip()
        else:
            # User chose not to scan
            print("📝 Manual WiFi configuration:")
            ssid = input("WiFi SSID: ").strip()
            if not ssid:
                print("❌ SSID cannot be empty")
                return
            password = input("WiFi Password (leave empty for open network): ").strip()
    
    print()
    result = provision_esp32(esp32_ip, ssid, password)
    
    print("\n" + "=" * 60)
    if result is True:
        print("🎉 PROVISIONING SUCCESSFUL!")
        print("   Your ESP32 is now connected to the WiFi network.")
        print("   Check your router's admin panel to find the ESP32's new IP address.")
    elif result is False:
        print("❌ PROVISIONING FAILED!")
        print("   Please check the error messages above and try again.")
    else:
        print("❓ PROVISIONING STATUS UNKNOWN")
        print("   The ESP32 may have connected but confirmation timed out.")
        print("   Check your router's admin panel to see if the ESP32 is connected.")
    print("=" * 60)

if __name__ == "__main__":
    main()
