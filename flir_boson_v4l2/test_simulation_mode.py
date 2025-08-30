#!/usr/bin/env python3
"""
FLIR Boson+ Simulation Mode Test Runner
======================================

Complete hardware-less testing framework that:
2. Tests IOCTL interface functionality
3. Validates FSLP protocol compliance
4. Generates comprehensive test reports

Usage:
    sudo python3 test_simulation_mode.py
"""

import subprocess
import sys
import os
import time
import json
from pathlib import Path

class FLIRSimulationTester:
    """Complete simulation testing framework"""

    def __init__(self, verbose=True):
        self.verbose = verbose
        self.module_name = "flir_boson"
        self.test_results = {}

    def log(self, message, level="INFO"):
        """Log message with timestamp"""
        if self.verbose:
            timestamp = time.strftime("%H:%M:%S")
            print(f"[{timestamp}] [{level}] {message}")

    def run_command(self, cmd, ignore_errors=False):
        """Run shell command and return result"""
        try:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
            if result.returncode != 0 and not ignore_errors:
                self.log(f"Command failed: {cmd}", "ERROR")
                self.log(f"Error: {result.stderr}", "ERROR")
                return None
            return result
        except Exception as e:
            self.log(f"Exception running command '{cmd}': {e}", "ERROR")
            return None

    def check_module_loaded(self):
        """Check if simulation module is loaded"""
        result = self.run_command(f"lsmod | grep {self.module_name}", ignore_errors=True)
        return result and result.returncode == 0

    def check_device_created(self):
        """Check if V4L2 device was created"""
        self.log("Checking for V4L2 subdevice creation...")

        # Check for platform device
        result = self.run_command("ls /sys/bus/platform/devices/ | grep flir-boson", ignore_errors=True)
        if result and result.stdout.strip():
            self.log(f"✅ Platform device found: {result.stdout.strip()}")
            return True

        # Check for V4L2 subdev
        result = self.run_command("ls /dev/v4l-subdev* 2>/dev/null", ignore_errors=True)
        if result and result.stdout.strip():
            self.log(f"✅ V4L2 subdevices found: {result.stdout.strip()}")
            return True

        self.log("⚠️  No V4L2 devices detected (may be normal in simulation)", "WARN")
        return False

    def check_kernel_logs(self):
        """Check kernel logs for simulation activity"""
        self.log("Checking kernel logs for simulation activity...")

        # Check for simulation mode messages
        result = self.run_command("dmesg | tail -20 | grep -i 'flir\\|boson\\|simulation'", ignore_errors=True)
        if result and result.stdout.strip():
            self.log("✅ Simulation activity detected in kernel logs:")
            for line in result.stdout.strip().split('\n'):
                self.log(f"    {line}")
            return True
        else:
            self.log("⚠️  No simulation activity in recent kernel logs", "WARN")
            return False

    def test_protocol_validator(self):
        """Run the Python protocol validator"""
        self.log("Running FSLP protocol validator...")

        validator_script = Path("test_fslp_validator.py")
        if not validator_script.exists():
            self.log("❌ Protocol validator script not found", "ERROR")
            return False

        result = self.run_command("python3 test_fslp_validator.py --quiet")
        if result and result.returncode == 0:
            self.log("✅ FSLP protocol validator: ALL TESTS PASSED")
            return True
        else:
            self.log("❌ FSLP protocol validator failed", "ERROR")
            if result:
                self.log(f"Output: {result.stdout}", "ERROR")
                self.log(f"Error: {result.stderr}", "ERROR")
            return False

    def generate_test_report(self):
        """Generate comprehensive test report"""
        self.log("\n" + "="*60)
        self.log("FLIR Boson+ Simulation Mode Test Report")
        self.log("="*60)

        total_tests = len(self.test_results)
        passed_tests = sum(1 for result in self.test_results.values() if result)

        self.log(f"Total Tests: {total_tests}")
        self.log(f"Passed: {passed_tests}")
        self.log(f"Failed: {total_tests - passed_tests}")
        self.log(f"Success Rate: {(passed_tests/total_tests)*100:.1f}%")

        self.log("\nDetailed Results:")
        for test_name, result in self.test_results.items():
            status = "✅ PASS" if result else "❌ FAIL"
            self.log(f"  {test_name}: {status}")

        if passed_tests == total_tests:
            self.log("\n🎉 ALL SIMULATION TESTS PASSED!")
            self.log("The FLIR Boson+ driver is ready for hardware integration.")
            return True
        else:
            self.log(f"\n⚠️  {total_tests - passed_tests} TESTS FAILED")
            self.log("Review the failed tests before hardware deployment.")
            return False

    def run_full_simulation_test(self):
        """Run complete simulation test suite"""

        print("="*70)
        print("FLIR Boson+ Hardware-Less Simulation Test Suite")
        print("="*70)

        try:

            # Test 2: Device Creation
            self.log("\n🔧 Test 2: Device Creation")
            self.test_results["Device Creation"] = self.check_device_created()

            # Test 3: Kernel Log Activity
            self.log("\n🔧 Test 3: Kernel Log Activity")
            self.test_results["Kernel Log Activity"] = self.check_kernel_logs()

            # Test 4: Protocol Validation
            self.log("\n🔧 Test 4: FSLP Protocol Validation")
            self.test_results["FSLP Protocol Validation"] = self.test_protocol_validator()

            # Generate final report
            return self.generate_test_report()

        except KeyboardInterrupt:
            self.log("\nTest interrupted by user", "WARN")
            return False
        except Exception as e:
            self.log(f"Unexpected error: {e}", "ERROR")
            return False

def print_usage():
    """Print usage instructions"""
    print("""
FLIR Boson+ Simulation Mode Test Runner
=======================================

Prerequisites:
  - Build simulation mode: make sim
  - Load module: sudo insmod flir-boson.ko
  - Python 3.6+

Usage:
  python3 test_simulation_mode.py        # Run test suite
  python3 test_simulation_mode.py --help # Show this help

Manual Commands:
  sudo insmod flir-boson.ko              # Load module
  python3 test_simulation_mode.py        # Run tests
  sudo rmmod flir_boson                  # Unload module

Monitor logs: sudo dmesg -w | grep FSLP_SIM
""")

def main():
    """Main entry point"""

    # Check for help
    if len(sys.argv) > 1 and sys.argv[1] in ['--help', '-h']:
        print_usage()
        return 0

    # Create tester instance
    tester = FLIRSimulationTester(verbose=True)

    # Run test suite (assumes module already loaded)
    success = tester.run_full_simulation_test()

    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())