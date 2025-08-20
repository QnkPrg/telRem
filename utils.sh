#!/bin/bash
# ESP32 Audio Communication Project Management Script
# Handles ESP32 firmware, Python server, and project operations

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ESP32_DIR="$PROJECT_ROOT/esp32_firmware"
PYTHON_DIR="$PROJECT_ROOT/python_server"
BUILD_DIR="$PROJECT_ROOT/build_output"

echo "=== ESP32 Audio Communication Project ==="
echo "📁 Project Root: $PROJECT_ROOT"
echo ""

# Function to show help
show_help() {
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  build         - Build ESP32 firmware"
    echo "  flash         - Flash ESP32 firmware"
    echo "  monitor       - Monitor ESP32 output"
    echo "  clean         - Clean build outputs"
    echo "  server        - Start Python UDP server"
    echo "  install-deps  - Install Python dependencies"
    echo "  test          - Run Python tests"
    echo "  docs          - Show project documentation"
    echo "  help          - Show this help"
    echo ""
}

# Function to build ESP32 firmware
build_firmware() {
    echo "🔨 Building ESP32 firmware..."
    cd "$ESP32_DIR"
    if command -v idf.py &> /dev/null; then
        idf.py build
        echo "✅ Build complete"
    else
        echo "❌ ESP-IDF not found. Please install and setup ESP-IDF."
        exit 1
    fi
}

# Function to flash firmware
flash_firmware() {
    echo "📡 Flashing ESP32 firmware..."
    cd "$ESP32_DIR"
    idf.py flash
}

# Function to monitor ESP32
monitor_esp32() {
    echo "📺 Starting ESP32 monitor..."
    cd "$ESP32_DIR"
    idf.py monitor
}

# Function to clean build
clean_build() {
    echo "🧹 Cleaning build outputs..."
    rm -rf "$BUILD_DIR/build"
    rm -f "$BUILD_DIR/buildOut.*"
    rm -f "$BUILD_DIR/dependencies.lock"
    if [ -d "$ESP32_DIR/build" ]; then
        rm -rf "$ESP32_DIR/build"
    fi
    echo "✅ Clean complete"
}

# Function to start Python server
start_server() {
    echo "🖥️ Starting Python UDP server..."
    cd "$PYTHON_DIR"
    if command -v python3 &> /dev/null; then
        python3 server_udp.py
    else
        echo "❌ Python3 not found"
        exit 1
    fi
}

# Function to install Python dependencies
install_python_deps() {
    echo "📦 Installing Python dependencies..."
    cd "$PYTHON_DIR"
    pip3 install -r requirements.txt
    echo "✅ Dependencies installed"
}

# Function to run Python tests
run_tests() {
    echo "🧪 Running Python tests..."
    cd "$PYTHON_DIR/tests"
    python3 test_audio_server.py
}

# Function to show documentation
show_docs() {
    echo "📖 ESP32 Audio Communication Project Documentation"
    echo ""
    echo "📁 Project Structure:"
    echo "  docs/           - Documentation files"
    echo "  python_server/  - Python audio server"
    echo "  esp32_firmware/ - ESP32 source code"
    echo "  build_output/   - Build artifacts"
    echo ""
    echo "🔧 Quick Commands:"
    echo "  ./esp32-project build        - Build ESP32 firmware"
    echo "  ./esp32-project flash        - Flash ESP32 firmware"
    echo "  ./esp32-project monitor      - Monitor ESP32 output"
    echo "  ./esp32-project server       - Start Python UDP server"
    echo "  ./esp32-project install-deps - Install Python dependencies"
    echo "  ./esp32-project test         - Run Python tests"
    echo "  ./esp32-project clean        - Clean all build outputs"
    echo ""
    echo "📖 Documentation files:"
    echo "  docs/README.md               - Main project documentation"
    echo "  docs/AUDIO_SERVER_UDP.md     - UDP server documentation"
    echo "  python_server/README.md      - Python server guide"
    echo "  PROJECT_STRUCTURE.md         - This project structure"
    echo ""
    echo "🏗️ Architecture: Real-time bidirectional audio over UDP"
    echo "  [Microphone] → [Python Server] → [UDP] → [ESP32]"
    echo "  [Speakers]   ← [Audio Buffer]  ← [UDP] ← [ESP32 Audio]"
}

# Main command processing
case "${1:-help}" in
    build)
        build_firmware
        ;;
    flash)
        flash_firmware
        ;;
    monitor)
        monitor_esp32
        ;;
    clean)
        clean_build
        ;;
    server)
        start_server
        ;;
    install-deps)
        install_python_deps
        ;;
    test)
        run_tests
        ;;
    docs)
        show_docs
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        echo "❌ Unknown command: $1"
        echo ""
        show_help
        exit 1
        ;;
esac
