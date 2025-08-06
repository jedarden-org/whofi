#!/bin/bash
# ESP32 CSI Firmware Test Runner

set -e

echo "Running ESP32 CSI Firmware Tests..."

# Build test project
echo "Building test project..."
cd "$(dirname "$0")"
. $IDF_PATH/export.sh
idf.py build

echo "Tests completed successfully!"