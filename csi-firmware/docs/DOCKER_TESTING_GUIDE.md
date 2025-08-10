# Docker Testing Guide for ESP32-S3 CSI Firmware

## Overview

This guide explains how to perform comprehensive testing of the ESP32-S3 CSI firmware using Docker-based ESP-IDF environment. This is intended for subsequent Claude Code invocations and developers working on the firmware.

---

## 🐳 Docker Environment Setup

### Prerequisites
- Docker installed and running
- ESP32-S3 development board (SuperMini or DevKit)
- USB connection for flashing/monitoring

### ESP-IDF Docker Image
The firmware uses ESP-IDF v5.1.2 in a Docker container for reproducible builds and testing.

```bash
# Pull the official ESP-IDF Docker image
docker pull espressif/idf:v5.1.2

# Alternative: Use the project's custom Dockerfile
docker build -f Dockerfile.esp-idf -t csi-firmware-dev .
```

---

## 🧪 Test Configurations

### 4MB Flash Optimization Test
Test the optimized firmware for ESP32-S3 SuperMini (4MB flash):

```bash
# Enter firmware directory
cd /workspaces/ardenone-cluster-ws/whofi-org/csi-firmware

# Run 4MB optimization test in Docker
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  cp sdkconfig.4mb_minimal sdkconfig
  cp partitions_4mb_ota.csv partitions.csv
  idf.py clean
  idf.py build
  idf.py size
"
```

### Standard 8MB Test
Test the standard firmware configuration:

```bash
# Run standard configuration test
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  cp sdkconfig.ota sdkconfig
  cp partitions_ota.csv partitions.csv  
  idf.py clean
  idf.py build
  idf.py size
"
```

---

## 📊 Size Validation Tests

### Firmware Size Analysis
Validate that the firmware meets size requirements:

```bash
# Check firmware binary size
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  idf.py build
  ls -la build/*.bin
  idf.py size-components
  idf.py size-files
"
```

### Expected Results
- **4MB config**: Firmware should be ~580KB
- **8MB config**: Firmware should be ~800KB-1MB
- **OTA partitions**: Should fit within 1.625MB (4MB) or 1.25MB (8MB)

---

## 🔧 Component Testing

### CSI Module Test
Test CSI data collection functionality:

```bash
# Build and run CSI tests
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  cd test-tdd
  idf.py build
  # Flash and monitor would require hardware connection
"
```

### Memory Optimization Test
Validate memory optimizations:

```bash
# Test memory usage with different configurations
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  echo 'Testing 4MB optimization...'
  cp sdkconfig.4mb_minimal sdkconfig
  idf.py build
  idf.py size | grep 'Total image size'
  
  echo 'Testing standard configuration...'
  cp sdkconfig.defaults sdkconfig
  idf.py clean && idf.py build
  idf.py size | grep 'Total image size'
"
```

---

## 🏗️ Build Validation

### Multi-Configuration Build Test
Test all supported configurations:

```bash
#!/bin/bash
# Multi-config build test script

CONFIGS=("sdkconfig.4mb_minimal" "sdkconfig.ota" "sdkconfig.defaults")
PARTITIONS=("partitions_4mb_ota.csv" "partitions_ota.csv" "partitions.csv")

for i in "${!CONFIGS[@]}"; do
    CONFIG="${CONFIGS[$i]}"
    PARTITION="${PARTITIONS[$i]}"
    
    echo "Testing configuration: $CONFIG"
    
    docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
        cp $CONFIG sdkconfig
        cp $PARTITION partitions.csv
        idf.py clean
        if idf.py build; then
            echo '✅ Build successful for $CONFIG'
            idf.py size | grep 'Total image size'
        else
            echo '❌ Build failed for $CONFIG'
        fi
    "
done
```

---

## 📡 Hardware-in-Loop Testing

### Flash and Monitor
When hardware is available:

```bash
# Flash firmware to ESP32-S3
docker run --rm --device=/dev/ttyUSB0 -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  idf.py flash monitor
"

# Alternative with specific port
docker run --rm --device=/dev/ttyACM0 -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  idf.py -p /dev/ttyACM0 flash monitor
"
```

### OTA Update Test
Test OTA functionality:

```bash
# Build OTA-enabled firmware
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  cp sdkconfig.4mb_minimal sdkconfig
  cp partitions_4mb_ota.csv partitions.csv
  idf.py build
  
  # Generate OTA update package
  esptool.py --chip esp32s3 merge_bin -o build/ota_update.bin \
    --flash_mode dio --flash_freq 80m --flash_size 4MB \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/csi_firmware.bin
"
```

---

## 🧪 Test-Driven Development (TDD)

### Run TDD Test Suite
Execute the comprehensive test suite:

```bash
# Run all TDD tests
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  # Build test suite
  cd test-tdd
  idf.py build
  
  # Run unit tests (requires hardware or simulator)
  echo 'TDD tests require hardware connection for full validation'
  echo 'See test_4mb_optimized.c for test definitions'
"
```

### Test Categories
The TDD suite covers:
1. **4MB Flash & Partition Validation** (4 tests)
2. **Memory Optimization** (3 tests)
3. **CSI Functionality** (3 tests)
4. **Web Server** (2 tests)
5. **Communication Protocols** (2 tests)
6. **OTA System** (2 tests)
7. **Time Synchronization** (2 tests)
8. **System Health** (2 tests)

---

## 📊 Performance Benchmarking

### Firmware Size Benchmark
Compare different build configurations:

```bash
# Size benchmark script
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  echo '=== Firmware Size Benchmark ==='
  
  echo 'Configuration,Binary Size,Flash Used,RAM Used'
  
  for config in sdkconfig.4mb_minimal sdkconfig.ota sdkconfig.defaults; do
    cp \$config sdkconfig
    idf.py clean > /dev/null 2>&1
    if idf.py build > /dev/null 2>&1; then
      size_info=\$(idf.py size 2>/dev/null | grep 'Total sizes')
      echo \"\$config,\$size_info\"
    fi
  done
"
```

### Memory Usage Analysis
Analyze memory consumption:

```bash
# Memory analysis
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  idf.py build
  idf.py size-components | head -20
  echo '=== Memory Map ==='
  idf.py size-files | head -10
"
```

---

## 🔍 Debugging and Diagnostics

### Build Diagnostics
Diagnose build issues:

```bash
# Verbose build with diagnostics
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  idf.py clean
  idf.py build -v | tee build.log
  
  # Check for common issues
  grep -i 'error\|failed\|warning' build.log || echo 'No build issues found'
"
```

### Configuration Validation
Validate sdkconfig settings:

```bash
# Check configuration conflicts
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 bash -c "
  idf.py menuconfig --help
  # Manual inspection of sdkconfig required
"
```

---

## 🚀 Automated Testing Pipeline

### CI/CD Integration
For continuous integration:

```yaml
# .github/workflows/firmware-test.yml
name: ESP32-S3 Firmware Tests
on: [push, pull_request]

jobs:
  test-4mb-config:
    runs-on: ubuntu-latest
    container: espressif/idf:v5.1.2
    steps:
      - uses: actions/checkout@v3
      - name: Test 4MB Configuration
        run: |
          cd csi-firmware
          cp sdkconfig.4mb_minimal sdkconfig
          cp partitions_4mb_ota.csv partitions.csv
          idf.py build
          idf.py size
```

### Test Runner Script
Use the provided test runner:

```bash
# Execute comprehensive test suite
cd /workspaces/ardenone-cluster-ws/whofi-org/csi-firmware
chmod +x test_4mb_runner.sh

# Run with Docker
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 ./test_4mb_runner.sh
```

---

## 📋 Quick Reference

### Essential Commands
```bash
# Build firmware
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 idf.py build

# Check size
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 idf.py size

# Clean build
docker run --rm -v $(pwd):/project -w /project espressif/idf:v5.1.2 idf.py clean

# Flash (with hardware)
docker run --rm --device=/dev/ttyUSB0 -v $(pwd):/project -w /project espressif/idf:v5.1.2 idf.py flash
```

### Configuration Files
- `sdkconfig.4mb_minimal` - 4MB SuperMini optimization
- `sdkconfig.ota` - Standard OTA configuration  
- `sdkconfig.defaults` - Default configuration
- `partitions_4mb_ota.csv` - 4MB OTA partition table
- `partitions_ota.csv` - Standard OTA partitions

### Test Files
- `test-tdd/test_4mb_optimized.c` - Comprehensive TDD suite
- `test_4mb_runner.sh` - Automated test runner
- `docs/FIRMWARE_FEATURES.md` - Complete feature list

---

## 🎯 Success Criteria

### 4MB Configuration Tests
- ✅ Firmware size < 580KB
- ✅ Fits in 1.625MB OTA partition
- ✅ All features functional
- ✅ Memory usage optimized

### Standard Configuration Tests  
- ✅ Firmware size < 1MB
- ✅ Fits in 1.25MB OTA partition
- ✅ Full feature set
- ✅ Production ready

### Hardware Compatibility
- ✅ ESP32-S3 SuperMini (4MB)
- ✅ ESP32-S3-DevKitC-1 (8MB+)
- ✅ OTA updates functional
- ✅ CSI positioning accurate

This guide ensures comprehensive testing of the ESP32-S3 CSI firmware in Docker environment with reproducible results across different development environments.