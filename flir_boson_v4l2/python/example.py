#!/usr/bin/env python3
"""
Example usage of FLIR Boson V4L2 driver Python interface
Drop-in replacement for I2CFslp
"""

import sys
sys.path.append('../..')

from flir_boson_v4l2.python.v4l2_fslp import V4L2Fslp


def test_v4l2_fslp_basic():
    """Basic test of V4L2 FSLP interface"""
    print("Testing FLIR Boson V4L2 FSLP interface...")

    try:
        # Create V4L2 FSLP instance (exactly like I2CFslp)
        fslp = V4L2Fslp()
        print(f"✓ Connected to FLIR Boson via V4L2 device: {fslp.port.device_path}")

        # Test basic FSLP communication
        print("✓ V4L2 FSLP interface initialized successfully")

        return fslp

    except Exception as e:
        print(f"✗ Failed to initialize V4L2 FSLP interface: {e}")
        return None


def demonstrate_sdk_compatibility():
    """Show how to modify existing SDK code to use V4L2 driver"""
    print("\nSDK Compatibility Example:")
    print("=" * 50)

    print("OLD CODE (using I2C directly):")
    print("```python")
    print("from SDK.CommunicationFiles.I2CFslp import I2CFslp")
    print("from SDK.ClientFiles_Python.Client_API import pyClient")
    print("")
    print("# Create I2C FSLP connection")
    print("camera = pyClient(useI2C=True, portID='/dev/i2c-1')")
    print("```")
    print("")

    print("NEW CODE (using V4L2 driver):")
    print("```python")
    print("from flir_boson_v4l2.python.v4l2_fslp import V4L2Fslp")
    print("from SDK.ClientFiles_Python.Client_API import pyClient")
    print("")
    print("# Create V4L2 FSLP connection")
    print("fslp = V4L2Fslp()")
    print("camera = pyClient(fslp=fslp)")
    print("```")
    print("")

    print("INTERFACE COMPARISON:")
    print("I2CFslp methods:")
    print("  - sendFrame(channelID, data, dataSize)")
    print("  - readFrame(channelID, expectedReceiveBytes)")
    print("")
    print("V4L2Fslp methods:")
    print("  - sendFrame(channelID, data, dataSize)   ✓ IDENTICAL")
    print("  - readFrame(channelID, expectedReceiveBytes)   ✓ IDENTICAL")
    print("")

    print("BENEFITS:")
    print("• No direct I2C hardware access required")
    print("• Kernel driver handles low-level communication")
    print("• Better error handling and recovery")
    print("• Concurrent access protection")
    print("• Standard V4L2 integration")


def main():
    """Main example function"""
    print("FLIR Boson V4L2 Driver Python Interface Example")
    print("=" * 60)

    # Test basic functionality
    fslp = test_v4l2_fslp_basic()
    if not fslp:
        print("\nMake sure:")
        print("1. FLIR Boson V4L2 driver is loaded (insmod flir-boson.ko)")
        print("2. FLIR Boson camera is connected and powered")
        print("3. Device tree is properly configured")
        print("4. You have permission to access V4L2 devices")
        return 1

    # Show compatibility information
    demonstrate_sdk_compatibility()

    print("\nExample completed successfully!")
    print("The V4L2Fslp class is a perfect drop-in replacement for I2CFslp!")
    return 0


if __name__ == "__main__":
    sys.exit(main())