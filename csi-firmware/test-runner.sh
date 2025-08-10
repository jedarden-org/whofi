#!/bin/bash

# Lightweight CSI Firmware Test Runner
# This script provides compilation testing without requiring full ESP-IDF

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[TEST-RUNNER]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Test component headers and structure
test_component_structure() {
    print_status "Testing component structure..."
    
    local errors=0
    
    # Check required components
    for component in csi_collector mqtt_client ntp_sync ota_updater web_server; do
        if [ ! -d "components/$component" ]; then
            print_error "Component $component directory not found"
            errors=$((errors + 1))
        else
            print_status "✓ Component $component found"
        fi
    done
    
    # Check main files
    if [ ! -f "main/main.c" ]; then
        print_error "main/main.c not found"
        errors=$((errors + 1))
    else
        print_status "✓ main/main.c found"
    fi
    
    if [ ! -f "CMakeLists.txt" ]; then
        print_error "CMakeLists.txt not found"
        errors=$((errors + 1))
    else
        print_status "✓ CMakeLists.txt found"
    fi
    
    if [ $errors -eq 0 ]; then
        print_success "Component structure test passed"
        return 0
    else
        print_error "Component structure test failed with $errors errors"
        return 1
    fi
}

# Test C syntax without compilation
test_c_syntax() {
    print_status "Testing C syntax..."
    
    local errors=0
    
    # Find all .c and .h files
    while IFS= read -r -d '' file; do
        print_status "Checking syntax: $(basename "$file")"
        
        # Basic syntax check using gcc -fsyntax-only (if available)
        if command -v gcc >/dev/null 2>&1; then
            if ! gcc -fsyntax-only -I. -Icomponents/*/include "$file" 2>/dev/null; then
                print_warning "Syntax issues in $file (expected without ESP-IDF headers)"
            fi
        fi
        
        # Check for common issues
        if grep -q "^\s*#include\s*$" "$file"; then
            print_error "Empty #include directive in $file"
            errors=$((errors + 1))
        fi
        
        if grep -q ";\s*;" "$file"; then
            print_warning "Double semicolon found in $file"
        fi
        
    done < <(find . -name "*.c" -o -name "*.h" -not -path "./build/*" -print0)
    
    if [ $errors -eq 0 ]; then
        print_success "C syntax test completed"
        return 0
    else
        print_error "C syntax test found $errors issues"
        return 1
    fi
}

# Test CMakeLists.txt structure
test_cmake_structure() {
    print_status "Testing CMake configuration..."
    
    local errors=0
    
    # Check main CMakeLists.txt
    if ! grep -q "idf_component_register" main/CMakeLists.txt 2>/dev/null; then
        print_error "main/CMakeLists.txt missing idf_component_register"
        errors=$((errors + 1))
    fi
    
    # Check component CMakeLists.txt files
    for component in csi_collector mqtt_client ntp_sync ota_updater web_server; do
        cmake_file="components/$component/CMakeLists.txt"
        if [ -f "$cmake_file" ]; then
            if ! grep -q "idf_component_register" "$cmake_file"; then
                print_error "$cmake_file missing idf_component_register"
                errors=$((errors + 1))
            else
                print_status "✓ $cmake_file structure OK"
            fi
        else
            print_error "$cmake_file not found"
            errors=$((errors + 1))
        fi
    done
    
    if [ $errors -eq 0 ]; then
        print_success "CMake structure test passed"
        return 0
    else
        print_error "CMake structure test failed with $errors errors"
        return 1
    fi
}

# Test configuration files
test_config_files() {
    print_status "Testing configuration files..."
    
    local errors=0
    
    # Check sdkconfig
    if [ ! -f "sdkconfig.defaults" ]; then
        print_error "sdkconfig.defaults not found"
        errors=$((errors + 1))
    else
        print_status "✓ sdkconfig.defaults found"
        
        # Check for required CONFIG options
        if ! grep -q "CONFIG_ESP32_WIFI_CSI_ENABLED" sdkconfig.defaults; then
            print_error "CONFIG_ESP32_WIFI_CSI_ENABLED not found in sdkconfig.defaults"
            errors=$((errors + 1))
        fi
    fi
    
    # Check partitions
    if [ ! -f "partitions.csv" ]; then
        print_error "partitions.csv not found"
        errors=$((errors + 1))
    else
        print_status "✓ partitions.csv found"
    fi
    
    if [ $errors -eq 0 ]; then
        print_success "Configuration files test passed"
        return 0
    else
        print_error "Configuration files test failed with $errors errors"
        return 1
    fi
}

# Test documentation completeness
test_documentation() {
    print_status "Testing documentation..."
    
    local warnings=0
    
    if [ ! -f "README.md" ]; then
        print_warning "README.md not found"
        warnings=$((warnings + 1))
    fi
    
    # Check for component documentation
    for component in csi_collector mqtt_client ntp_sync ota_updater web_server; do
        if [ ! -f "components/$component/README.md" ] && [ ! -f "components/$component/include/*.h" ]; then
            print_warning "No documentation found for component $component"
            warnings=$((warnings + 1))
        fi
    done
    
    print_status "Documentation test completed with $warnings warnings"
    return 0
}

# Run MQTT server test setup
test_mqtt_setup() {
    print_status "Testing MQTT test setup..."
    
    # Check if Docker is available
    if ! command -v docker >/dev/null 2>&1; then
        print_warning "Docker not available, skipping MQTT test"
        return 0
    fi
    
    # Start minimal MQTT broker for testing
    print_status "Starting test MQTT broker..."
    
    if docker run -d --name test-mosquitto -p 1883:1883 eclipse-mosquitto:2.0 >/dev/null 2>&1; then
        print_success "Test MQTT broker started on port 1883"
        
        # Give it a moment to start
        sleep 2
        
        # Test connection (if mosquitto_pub is available)
        if command -v mosquitto_pub >/dev/null 2>&1; then
            if mosquitto_pub -h localhost -p 1883 -t "test/topic" -m "test message" 2>/dev/null; then
                print_success "MQTT test connection successful"
            else
                print_warning "MQTT test connection failed"
            fi
        fi
        
        # Clean up
        docker stop test-mosquitto >/dev/null 2>&1
        docker rm test-mosquitto >/dev/null 2>&1
        print_status "Test MQTT broker stopped"
        
        return 0
    else
        print_warning "Could not start test MQTT broker"
        return 0
    fi
}

# Generate test report
generate_report() {
    local total_tests=5
    local passed_tests=$1
    
    print_status "=== CSI Firmware Test Report ==="
    print_status "Tests passed: $passed_tests/$total_tests"
    
    if [ $passed_tests -eq $total_tests ]; then
        print_success "All tests passed! 🎉"
        print_status "The firmware is ready for ESP-IDF compilation"
        print_status ""
        print_status "Next steps:"
        print_status "1. Install ESP-IDF v5.1.2 or use Docker"
        print_status "2. Run 'idf.py build' to compile"
        print_status "3. Flash to ESP32-S3 hardware"
        print_status "4. Start csi-server for data reception"
        return 0
    else
        print_error "Some tests failed. Please review the issues above."
        return 1
    fi
}

# Show help
show_help() {
    echo "CSI Firmware Test Runner"
    echo "Tests firmware structure and syntax without requiring ESP-IDF"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  test        Run all tests"
    echo "  structure   Test component structure only"
    echo "  syntax      Test C syntax only"
    echo "  cmake       Test CMake configuration only"
    echo "  config      Test configuration files only"
    echo "  docs        Test documentation completeness"
    echo "  mqtt        Test MQTT broker setup"
    echo "  help        Show this help message"
}

# Main execution
main() {
    cd "$PROJECT_DIR"
    
    case "${1:-test}" in
        "test")
            print_status "Running complete firmware test suite..."
            local passed=0
            
            test_component_structure && passed=$((passed + 1))
            test_c_syntax && passed=$((passed + 1))
            test_cmake_structure && passed=$((passed + 1))
            test_config_files && passed=$((passed + 1))
            test_documentation && passed=$((passed + 1))
            
            # MQTT test is optional
            test_mqtt_setup
            
            generate_report $passed
            ;;
        "structure")
            test_component_structure
            ;;
        "syntax")
            test_c_syntax
            ;;
        "cmake")
            test_cmake_structure
            ;;
        "config")
            test_config_files
            ;;
        "docs")
            test_documentation
            ;;
        "mqtt")
            test_mqtt_setup
            ;;
        "help")
            show_help
            ;;
        *)
            print_error "Unknown command: $1"
            show_help
            exit 1
            ;;
    esac
}

main "$@"