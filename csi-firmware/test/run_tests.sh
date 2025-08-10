#!/bin/bash
# ESP32 Firmware Test Runner
# Test-Driven Development for CSI Firmware Components

set -e

echo "🧪 Running ESP32 CSI Firmware Tests..."
echo "=================================================="

# Test 1: Build Validation
echo "📦 Test 1: Build Validation"
if [ -f "../build/csi_firmware.bin" ]; then
    FIRMWARE_SIZE=$(stat -f%z "../build/csi_firmware.bin" 2>/dev/null || stat -c%s "../build/csi_firmware.bin" 2>/dev/null)
    echo "✅ Firmware binary exists: csi_firmware.bin (${FIRMWARE_SIZE} bytes)"
    
    # Test firmware size constraint (must be under 1MB for non-OTA ESP32)
    MAX_SIZE=1048576  # 1MB in bytes
    if [ "$FIRMWARE_SIZE" -lt "$MAX_SIZE" ]; then
        echo "✅ Firmware size OK: ${FIRMWARE_SIZE} bytes < ${MAX_SIZE} bytes (1MB limit)"
    else
        echo "❌ Firmware too large: ${FIRMWARE_SIZE} bytes > ${MAX_SIZE} bytes"
        exit 1
    fi
else
    echo "❌ Firmware binary not found: build/csi_firmware.bin"
    exit 1
fi

# Test 2: Configuration Validation
echo "📋 Test 2: Configuration Validation"
if [ -f "../sdkconfig" ]; then
    echo "✅ SDK configuration exists"
    
    # Check critical CSI configuration
    if grep -q "CONFIG_ESP32_WIFI_CSI_ENABLED=y" ../sdkconfig; then
        echo "✅ CSI enabled in configuration"
    else
        echo "❌ CSI not enabled in configuration"
        exit 1
    fi
    
    # Check memory optimization
    if grep -q "CONFIG_COMPILER_OPTIMIZATION_SIZE=y" ../sdkconfig 2>/dev/null; then
        echo "✅ Size optimization enabled"
    else
        echo "⚠️  Size optimization not explicitly set"
    fi
else
    echo "❌ SDK configuration not found"
    exit 1
fi

# Test 3: Component Structure Validation
echo "🏗️  Test 3: Component Structure Validation"
REQUIRED_COMPONENTS=(
    "../components/csi_collector"
    "../components/mqtt_client" 
    "../components/ntp_sync"
    "../components/web_server"
    "../components/ota_updater"
)

for component in "${REQUIRED_COMPONENTS[@]}"; do
    if [ -d "$component" ]; then
        echo "✅ Component exists: $(basename $component)"
        
        # Check component.mk or CMakeLists.txt
        if [ -f "$component/CMakeLists.txt" ] || [ -f "$component/component.mk" ]; then
            echo "  📝 Build configuration found"
        else
            echo "  ⚠️  No build configuration found"
        fi
    else
        echo "❌ Component missing: $(basename $component)"
        exit 1
    fi
done

# Test 4: Memory Layout Validation
echo "🧠 Test 4: Memory Layout Validation"
if [ -f "../partitions.csv" ] || [ -f "../partitions_4mb_ota.csv" ]; then
    echo "✅ Partition table exists"
    
    # Check for OTA partitions
    PARTITION_FILE="../partitions.csv"
    [ -f "../partitions_4mb_ota.csv" ] && PARTITION_FILE="../partitions_4mb_ota.csv"
    
    if grep -q "ota_" "$PARTITION_FILE"; then
        echo "✅ OTA partitions configured"
    else
        echo "ℹ️  Standard partitions (no OTA)"
    fi
else
    echo "❌ Partition table not found"
    exit 1
fi

# Test 5: Build Environment Validation
echo "🔧 Test 5: Build Environment Validation"
if [ -f "../CMakeLists.txt" ]; then
    echo "✅ CMake build system configured"
else
    echo "❌ CMakeLists.txt not found"
    exit 1
fi

# Test 6: Version and Metadata
echo "📊 Test 6: Version and Metadata"
if [ -f "../main/main.c" ] || [ -f "../main/app_main.c" ]; then
    echo "✅ Main application file exists"
else
    echo "❌ Main application file not found"
    exit 1
fi

echo "=================================================="
echo "🎉 All tests passed! Firmware ready for deployment."
echo "✨ ESP32-S3 CSI firmware validated successfully."
echo "=================================================="

exit 0