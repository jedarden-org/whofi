# Docker-based ESP-IDF Development for CSI Firmware

## Quick Start

### 1. Build ESP-IDF Docker Image
```bash
./docker-build.sh build-image
```

### 2. Start Development Environment 
```bash
./docker-build.sh start
```

### 3. Build Firmware
```bash
./docker-build.sh build
```

### 4. Run Tests
```bash
./docker-build.sh test
```

### 5. Interactive Development
```bash
./docker-build.sh shell
# Inside container:
idf.py build
idf.py flash  # If ESP32 connected via USB
idf.py monitor
```

## Available Commands

- `build-image` - Build ESP-IDF Docker image (one-time)
- `start` - Start development environment with MQTT test server
- `stop` - Stop development environment
- `shell` - Open interactive bash shell in container
- `build` - Build CSI firmware
- `test` - Build and run unit tests
- `build-tests` - Build tests only
- `clean` - Clean build artifacts
- `analyze` - Run static code analysis (clang-tidy, cppcheck)
- `logs` - Show test server logs

## Environment Features

### ESP-IDF Container
- ESP-IDF v5.1.2 pre-installed
- All ESP32-S3 toolchains ready
- Static analysis tools (clang-tidy, cppcheck)
- USB device passthrough for hardware flashing

### Test Infrastructure
- MQTT broker for CSI data testing
- Node.js test server for data reception
- WebSocket interface for browser testing
- Docker networking for component communication

## Testing Without Hardware

The Docker environment allows you to:
- ✅ Compile all firmware components
- ✅ Run unit tests (mocked hardware)
- ✅ Test MQTT communication
- ✅ Validate CSI data structures
- ✅ Run static code analysis
- ✅ Test server integration

## Hardware Testing (Optional)

Connect ESP32 via USB and use:
```bash
./docker-build.sh shell
# Inside container:
idf.py flash monitor
```

## Troubleshooting

### Permission Issues
```bash
# Fix USB device permissions
sudo usermod -aG dialout $USER
sudo chmod 666 /dev/ttyUSB*
```

### Container Issues
```bash
# Rebuild container
./docker-build.sh stop
docker system prune -f
./docker-build.sh build-image
./docker-build.sh start
```