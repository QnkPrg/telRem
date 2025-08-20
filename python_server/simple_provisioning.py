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
        print("Scanning for available WiFi networks...")
        response = requests.get(f"http://{esp32_ip}/scan", timeout=15)  # Scan can take a while
        
        if response.status_code == 200:
            scan_result = response.json()
            networks = scan_result.get('networks', [])
            count = scan_result.get('count', 0)
            
            if count == 0:
                print("   No WiFi networks found")
                return []
            
            print(f"   Found {count} WiFi networks:")
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
            print(f"   Scan failed (HTTP {response.status_code})")
            return []
            
    except requests.exceptions.RequestException as e:
        print(f"   Scan failed: {e}")
        return []

def provision_esp32(esp32_ip, ssid, password):
    """
    Provision ESP32 with WiFi credentials using simple HTTP requests
    """
    base_url = f"http://{esp32_ip}"
    
    print(f"Starting provisioning for ESP32 at {esp32_ip}")
    print(f"WiFi SSID: {ssid}")
    print(f"WiFi Password: {'*' * len(password) if password else '(open network)'}")
    print()
    
    try:       
        # Send WiFi credentials
        print("\n Sending WiFi credentials...")
        credentials = {
            "ssid": ssid,
            "password": password
        }
        
        response = requests.post(
            f"{base_url}/config", 
            json=credentials,
            headers={'Content-Type': 'application/json'},
            timeout=30  # Increased timeout as ESP32 needs time to connect
        )
        
        if response.status_code == 200:
            result = response.json()
            if result.get('success'):
                print("   SUCCESS! ESP32 connected to WiFi network")
                print("   Provisioning completed successfully")
                return True
            else:
                return False
        else:
            print(f"   Failed to connect (HTTP {response.status_code})")
            print(f"   Reason: {response.reason}")
            return False
        
    except requests.exceptions.ConnectionError:
        print(f"Cannot connect to ESP32 at {esp32_ip}")
        print("   Make sure:")
        print("   • ESP32 is powered on and in provisioning mode")
        print("   • You're connected to the ESP32's WiFi AP")
        print("   • The IP address is correct (usually 192.168.4.1)")
        return False
    
    except requests.exceptions.Timeout:
        print("   Request timed out")
        print("   This might mean the ESP32 is connecting to WiFi")
        print("   Check your router's admin panel to see if ESP32 connected")
        return None  # Unknown status
        
    except Exception as e:
        print(f"Unexpected error: {e}")
        return False

def main():
    print("=" * 60)
    print("Simple ESP32 WiFi Provisioning")
    print("=" * 60)
    print()
    
    # Check for scan-only mode
    if len(sys.argv) == 3 and sys.argv[1] == "scan":
        esp32_ip = sys.argv[2]
        print(f"Scanning WiFi networks via ESP32 at {esp32_ip}")
        print()
        networks = scan_wifi_networks(esp32_ip)
        if networks:
            print(f"\nTo provision, run:")
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
        print("Enter provisioning details:")
        esp32_ip = input("ESP32 IP address (default: 192.168.4.1): ").strip()
        if not esp32_ip:
            esp32_ip = "192.168.4.1"
        
        print()
        
        # Ask if user wants to scan for networks
        scan_choice = input("Would you like to scan for available WiFi networks? (y/n, default: y): ").strip().lower()
        if scan_choice != 'n':
            networks = scan_wifi_networks(esp32_ip)
            
            if networks:
                print()
                while True:
                    choice = input("Select network by number, or type SSID manually (or 'q' to quit): ").strip()
                    
                    if choice.lower() == 'q':
                        print("Provisioning cancelled")
                        return
                    
                    # Check if user entered a number to select from scan results
                    try:
                        network_num = int(choice)
                        if 1 <= network_num <= len(networks):
                            selected_network = networks[network_num - 1]
                            ssid = selected_network['ssid']
                            security = selected_network['security']
                            
                            print(f"   Selected: '{ssid}' ({security})")
                            
                            # Ask for password if network is secured
                            if security.lower() != 'open':
                                password = input(f"Enter password for '{ssid}': ").strip()
                            else:
                                password = ""
                                print("   Open network - no password required")
                            break
                        else:
                            print(f"   Invalid selection. Choose 1-{len(networks)}")
                            continue
                    except ValueError:
                        # User typed an SSID manually
                        ssid = choice
                        if ssid:
                            print(f"   Manual SSID: {ssid}")
                            password = input(f"Enter password for '{ssid}' (leave empty for open network): ").strip()
                            break
                        else:
                            print("   SSID cannot be empty")
                            continue
            else:
                # Scan failed or no networks found, fall back to manual entry
                print("Manual WiFi configuration:")
                ssid = input("WiFi SSID: ").strip()
                if not ssid:
                    print("SSID cannot be empty")
                    return
                password = input("WiFi Password (leave empty for open network): ").strip()
        else:
            # User chose not to scan
            print("Manual WiFi configuration:")
            ssid = input("WiFi SSID: ").strip()
            if not ssid:
                print("SSID cannot be empty")
                return
            password = input("WiFi Password (leave empty for open network): ").strip()
    
    print()
    result = provision_esp32(esp32_ip, ssid, password)
    
    print("\n" + "=" * 60)
    if result is True:
        print("PROVISIONING SUCCESSFUL!")
        print("   Your ESP32 is now connected to the WiFi network.")
        print("   Check your router's admin panel to find the ESP32's new IP address.")
    elif result is False:
        print("PROVISIONING FAILED!")
        print("   Please check the error messages above and try again.")
    else:
        print("PROVISIONING STATUS UNKNOWN")
        print("   The ESP32 may have connected but confirmation timed out.")
        print("   Check your router's admin panel to see if the ESP32 is connected.")
    print("=" * 60)

if __name__ == "__main__":
    main()
