#!/usr/bin/env python3
"""
ESP32 CSI Firmware Test Framework
Test-Driven Development for ESP32-S3 CSI Components
"""

import os
import sys
import subprocess
import json
from pathlib import Path

class ESP32TestFramework:
    def __init__(self, firmware_dir="../"):
        self.firmware_dir = Path(firmware_dir)
        self.results = {
            "total_tests": 0,
            "passed": 0,
            "failed": 0,
            "errors": []
        }
    
    def run_test(self, test_name, test_func):
        """Run a single test and track results"""
        self.results["total_tests"] += 1
        print(f"🧪 Running: {test_name}")
        
        try:
            result = test_func()
            if result:
                print(f"✅ PASS: {test_name}")
                self.results["passed"] += 1
                return True
            else:
                print(f"❌ FAIL: {test_name}")
                self.results["failed"] += 1
                return False
        except Exception as e:
            print(f"💥 ERROR: {test_name} - {str(e)}")
            self.results["failed"] += 1
            self.results["errors"].append(f"{test_name}: {str(e)}")
            return False
    
    def test_build_artifacts(self):
        """Test 1: Build artifacts exist and are valid"""
        build_dir = self.firmware_dir / "build"
        
        # Check if build directory exists
        if not build_dir.exists():
            print("❌ Build directory not found")
            return False
        
        # Check for main firmware binary
        firmware_bin = build_dir / "csi_firmware.bin"
        if not firmware_bin.exists():
            print("❌ Main firmware binary not found")
            return False
        
        # Check firmware size (must be under 1MB for non-OTA)
        firmware_size = firmware_bin.stat().st_size
        max_size = 1024 * 1024  # 1MB
        
        if firmware_size > max_size:
            print(f"❌ Firmware too large: {firmware_size} > {max_size} bytes")
            return False
        
        print(f"✅ Firmware size OK: {firmware_size} bytes")
        return True
    
    def test_configuration_files(self):
        """Test 2: Configuration files are valid"""
        # Check sdkconfig
        sdkconfig = self.firmware_dir / "sdkconfig"
        if not sdkconfig.exists():
            print("❌ sdkconfig not found")
            return False
        
        # Read and validate critical configurations
        with open(sdkconfig, 'r') as f:
            config_content = f.read()
        
        critical_configs = [
            "CONFIG_ESP32_WIFI_CSI_ENABLED=y",
            "CONFIG_FREERTOS_UNICORE=n",  # Dual core for performance
        ]
        
        for config in critical_configs:
            if config not in config_content:
                print(f"❌ Missing critical configuration: {config}")
                return False
        
        print("✅ All critical configurations present")
        return True
    
    def test_component_structure(self):
        """Test 3: Component structure is valid"""
        components_dir = self.firmware_dir / "components"
        if not components_dir.exists():
            print("❌ Components directory not found")
            return False
        
        required_components = [
            "csi_collector",
            "mqtt_client",
            "ntp_sync", 
            "web_server",
            "ota_updater"
        ]
        
        for component in required_components:
            component_dir = components_dir / component
            if not component_dir.exists():
                print(f"❌ Component missing: {component}")
                return False
            
            # Check for build file
            cmake_file = component_dir / "CMakeLists.txt"
            component_mk = component_dir / "component.mk"
            
            if not cmake_file.exists() and not component_mk.exists():
                print(f"❌ Component {component} missing build configuration")
                return False
        
        print("✅ All required components present")
        return True
    
    def test_memory_layout(self):
        """Test 4: Memory layout and partitions"""
        # Check for partition table
        partition_files = [
            self.firmware_dir / "partitions.csv",
            self.firmware_dir / "partitions_4mb_ota.csv"
        ]
        
        partition_file = None
        for pf in partition_files:
            if pf.exists():
                partition_file = pf
                break
        
        if not partition_file:
            print("❌ No partition table found")
            return False
        
        # Validate partition table structure
        with open(partition_file, 'r') as f:
            partition_content = f.read()
        
        required_partitions = ["nvs", "phy_init", "app"]
        for partition in required_partitions:
            if partition not in partition_content:
                print(f"❌ Missing partition: {partition}")
                return False
        
        print("✅ Partition table valid")
        return True
    
    def test_build_system(self):
        """Test 5: Build system configuration"""
        cmake_file = self.firmware_dir / "CMakeLists.txt"
        if not cmake_file.exists():
            print("❌ CMakeLists.txt not found")
            return False
        
        with open(cmake_file, 'r') as f:
            cmake_content = f.read()
        
        # Check for project definition
        if "project(" not in cmake_content:
            print("❌ No project definition in CMakeLists.txt")
            return False
        
        print("✅ Build system configured")
        return True
    
    def test_main_application(self):
        """Test 6: Main application structure"""
        main_dir = self.firmware_dir / "main"
        if not main_dir.exists():
            print("❌ Main directory not found")
            return False
        
        # Look for main source files
        main_files = ["main.c", "app_main.c"]
        main_file = None
        
        for mf in main_files:
            if (main_dir / mf).exists():
                main_file = main_dir / mf
                break
        
        if not main_file:
            print("❌ No main application file found")
            return False
        
        # Check for basic app_main function
        with open(main_file, 'r') as f:
            main_content = f.read()
        
        if "app_main" not in main_content:
            print("❌ app_main function not found")
            return False
        
        print("✅ Main application structure valid")
        return True
    
    def run_all_tests(self):
        """Run all tests and report results"""
        print("🚀 Starting ESP32 CSI Firmware Test Suite")
        print("=" * 50)
        
        tests = [
            ("Build Artifacts", self.test_build_artifacts),
            ("Configuration Files", self.test_configuration_files), 
            ("Component Structure", self.test_component_structure),
            ("Memory Layout", self.test_memory_layout),
            ("Build System", self.test_build_system),
            ("Main Application", self.test_main_application)
        ]
        
        for test_name, test_func in tests:
            self.run_test(test_name, test_func)
            print()
        
        # Print summary
        print("=" * 50)
        print(f"📊 Test Results Summary:")
        print(f"   Total Tests: {self.results['total_tests']}")
        print(f"   Passed: {self.results['passed']}")
        print(f"   Failed: {self.results['failed']}")
        
        if self.results['errors']:
            print("❌ Errors:")
            for error in self.results['errors']:
                print(f"   - {error}")
        
        success_rate = (self.results['passed'] / self.results['total_tests']) * 100
        print(f"   Success Rate: {success_rate:.1f}%")
        print("=" * 50)
        
        # Return True if all tests passed
        return self.results['failed'] == 0

if __name__ == "__main__":
    test_framework = ESP32TestFramework()
    success = test_framework.run_all_tests()
    
    if success:
        print("🎉 All tests passed! Firmware ready for deployment.")
        sys.exit(0)
    else:
        print("💥 Some tests failed. Please fix issues before deployment.")
        sys.exit(1)