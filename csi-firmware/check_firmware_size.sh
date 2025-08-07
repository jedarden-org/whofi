#!/bin/bash
# Check firmware size for different flash configurations

echo "🔍 Checking firmware size compatibility..."

# Get the binary size
if [ -f "build/csi_collector.bin" ]; then
    FIRMWARE_SIZE=$(stat -c%s "build/csi_collector.bin" 2>/dev/null || stat -f%z "build/csi_collector.bin" 2>/dev/null)
    echo "📦 Firmware size: $(($FIRMWARE_SIZE / 1024)) KB"
    
    # Check for 1MB flash compatibility (896KB max for app partition)
    MAX_1MB_SIZE=$((896 * 1024))
    if [ $FIRMWARE_SIZE -le $MAX_1MB_SIZE ]; then
        echo "✅ Compatible with 1MB flash ESP32"
    else
        echo "❌ Too large for 1MB flash ESP32 (max: 896KB)"
        echo "   Current: $(($FIRMWARE_SIZE / 1024))KB, Limit: 896KB"
        echo "   Overflow: $((($FIRMWARE_SIZE - $MAX_1MB_SIZE) / 1024))KB"
        echo ""
        echo "💡 To build for 1MB flash, use:"
        echo "   idf.py -D SDKCONFIG_DEFAULTS='sdkconfig.1mb' build"
    fi
    
    # Check for 4MB flash compatibility  
    MAX_4MB_SIZE=$((2 * 1024 * 1024))
    if [ $FIRMWARE_SIZE -le $MAX_4MB_SIZE ]; then
        echo "✅ Compatible with 4MB flash ESP32"
    else
        echo "⚠️  Warning: Firmware approaching 4MB flash limit"
    fi
else
    echo "❌ Firmware binary not found. Build the project first."
    exit 1
fi

echo ""
echo "📋 Flash size options:"
echo "   - 1MB flash: Use partitions_1mb.csv (896KB app)"
echo "   - 4MB flash: Use partitions.csv (2MB app)"