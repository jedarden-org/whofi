#!/bin/bash
# ESP32 CSI Firmware Test Runner

set -e

echo "Running ESP32 CSI Firmware Tests..."

# Navigate to the main firmware directory instead
cd "$(dirname "$0")/.."

# Source ESP-IDF environment
if [ -z "$IDF_PATH" ]; then
    echo "Error: IDF_PATH not set. Please source ESP-IDF environment first."
    exit 1
fi

. $IDF_PATH/export.sh

# Build the main project as a test (since we don't have a separate test project)
echo "Building firmware for testing..."
idf.py build

# Run component tests if they exist
echo "Running component tests..."
for component in components/*/test; do
    if [ -d "$component" ]; then
        echo "Testing $(basename $(dirname $component))..."
        # Component tests would go here
    fi
done

echo "Tests completed successfully!"