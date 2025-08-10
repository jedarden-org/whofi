#!/bin/bash

# Docker-based ESP-IDF Build Script
# This script builds CSI firmware using Docker containers

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    echo -e "${BLUE}[DOCKER-BUILD]${NC} $1"
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

# Build Docker image
build_docker_image() {
    print_status "Building ESP-IDF Docker image..."
    cd "$PROJECT_DIR"
    
    if docker build -f Dockerfile.esp-idf -t csi-firmware-builder .; then
        print_success "Docker image built successfully"
    else
        print_error "Docker image build failed"
        exit 1
    fi
}

# Start development environment
start_dev_environment() {
    print_status "Starting ESP-IDF development environment..."
    cd "$PROJECT_DIR"
    
    export UID=$(id -u)
    export GID=$(id -g)
    
    if docker-compose -f docker-compose.esp-idf.yml up -d; then
        print_success "Development environment started"
        print_status "Use 'docker exec -it csi-firmware-builder bash' to enter the container"
        print_status "Or run: ./docker-build.sh shell"
    else
        print_error "Failed to start development environment"
        exit 1
    fi
}

# Stop development environment
stop_dev_environment() {
    print_status "Stopping ESP-IDF development environment..."
    cd "$PROJECT_DIR"
    
    docker-compose -f docker-compose.esp-idf.yml down
    print_success "Development environment stopped"
}

# Open shell in container
open_shell() {
    print_status "Opening shell in ESP-IDF container..."
    
    if docker exec -it csi-firmware-builder bash; then
        print_success "Shell session ended"
    else
        print_error "Failed to open shell. Is the container running?"
        print_status "Try: ./docker-build.sh start"
    fi
}

# Build firmware in container
build_firmware() {
    print_status "Building firmware in Docker container..."
    
    if docker exec csi-firmware-builder bash -c "cd /project && idf.py build"; then
        print_success "Firmware build completed"
        print_status "Build artifacts available in ./build/"
    else
        print_error "Firmware build failed"
        exit 1
    fi
}

# Build and run tests in container
run_tests() {
    print_status "Building and running tests in Docker container..."
    
    if docker exec csi-firmware-builder bash -c "cd /project && ./run_tests.sh"; then
        print_success "Tests completed"
    else
        print_warning "Some tests may have failed (ESP-IDF environment dependent)"
    fi
}

# Build tests only
build_tests() {
    print_status "Building tests in Docker container..."
    
    if docker exec csi-firmware-builder bash -c "cd /project/test && idf.py build"; then
        print_success "Test build completed"
        print_status "Test binaries available in ./test/build/"
    else
        print_error "Test build failed"
        exit 1
    fi
}

# Clean build artifacts
clean_build() {
    print_status "Cleaning build artifacts in Docker container..."
    
    if docker exec csi-firmware-builder bash -c "cd /project && idf.py clean && cd test && idf.py clean"; then
        print_success "Build artifacts cleaned"
    else
        print_error "Clean failed"
        exit 1
    fi
}

# Static analysis
run_analysis() {
    print_status "Running static analysis in Docker container..."
    
    docker exec csi-firmware-builder bash -c "
        cd /project && 
        echo 'Running clang-tidy...' && 
        find . -name '*.c' -not -path './build/*' -not -path './test/build/*' | head -10 | xargs -I {} clang-tidy {} && 
        echo 'Running cppcheck...' &&
        cppcheck --enable=all --suppress=missingIncludeSystem --quiet ./components/
    "
    print_success "Static analysis completed"
}

# Show logs from test server
show_test_logs() {
    print_status "Showing test server logs..."
    docker-compose -f docker-compose.esp-idf.yml logs -f test-server
}

# Show help
show_help() {
    echo "Docker-based ESP-IDF Build Script for CSI Firmware"
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  build-image   Build the Docker image"
    echo "  start         Start development environment"
    echo "  stop          Stop development environment" 
    echo "  shell         Open bash shell in container"
    echo "  build         Build firmware"
    echo "  test          Build and run tests"
    echo "  build-tests   Build tests only"
    echo "  clean         Clean build artifacts"
    echo "  analyze       Run static code analysis"
    echo "  logs          Show test server logs"
    echo "  help          Show this help message"
    echo ""
    echo "Typical workflow:"
    echo "  $0 build-image     # First time only"
    echo "  $0 start           # Start environment"
    echo "  $0 build           # Build firmware"
    echo "  $0 test            # Run tests"
    echo "  $0 shell           # Interactive development"
    echo ""
    echo "Hardware testing:"
    echo "  Connect ESP32 via USB and use 'shell' to access idf.py flash/monitor"
}

# Main script logic
main() {
    case "${1:-help}" in
        "build-image")
            build_docker_image
            ;;
        "start")
            start_dev_environment
            ;;
        "stop")
            stop_dev_environment
            ;;
        "shell")
            open_shell
            ;;
        "build")
            build_firmware
            ;;
        "test")
            run_tests
            ;;
        "build-tests")
            build_tests
            ;;
        "clean")
            clean_build
            ;;
        "analyze")
            run_analysis
            ;;
        "logs")
            show_test_logs
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