# FLIR Boson V4L2 Driver Python Interface

A Python interface for communicating with FLIR Boson+ thermal cameras through the V4L2 kernel driver using IOCTL calls. This provides a drop-in replacement for the I2C-based FSLP communication.

## Overview

This Python module provides a `V4L2Fslp` class that implements the same interface as the original `I2CFslp` class, allowing existing FLIR SDK code to work with the V4L2 driver without modification.

## Features

- **Drop-in replacement** for `I2CFslp` class
- **V4L2 driver integration** via IOCTL interface
- **Auto-detection** of FLIR Boson V4L2 devices
- **Compatible** with existing FLIR SDK Python code
- **Error handling** and device management

## Installation

No additional Python packages required - uses only standard library modules:
- `os`, `fcntl`, `struct`, `glob`, `typing`

Simply copy the `python/` directory to your project or add it to your Python path.

## Usage

### Basic Usage

```python
from flir_boson_v4l2.python import V4L2Fslp

# Create V4L2 FSLP interface (auto-detect device)
fslp = V4L2Fslp()

# Or specify device path explicitly
fslp = V4L2Fslp(portID="/dev/v4l-subdev0")
```

### SDK Integration

Replace I2C-based communication in existing FLIR SDK code:

**Old Code (I2C direct):**
```python
from SDK.CommunicationFiles.I2CFslp import I2CFslp
from SDK.ClientFiles_Python.Client_API import pyClient

# Using I2C directly
camera = pyClient(useI2C=True, portID='/dev/i2c-1')
```

**New Code (V4L2 driver):**
```python
from flir_boson_v4l2.python.v4l2_fslp import V4L2Fslp
from SDK.ClientFiles_Python.Client_API import pyClient

# Using V4L2 driver
fslp = V4L2Fslp()
camera = pyClient(fslp=fslp)

# All existing SDK commands work the same way
camera.dvoSetOutputInterface(1)  # Switch to MIPI
camera.dvoSetType(2)             # Set color mode
camera.dvoSetMipiState(2)        # Start streaming
```

### Example Script

```python
#!/usr/bin/env python3
from flir_boson_v4l2.python import V4L2Fslp

def main():
    try:
        # Connect to FLIR Boson via V4L2 driver
        fslp = V4L2Fslp()
        print(f"Connected to {fslp.port.device_path}")

        # Use with FLIR SDK
        # camera = pyClient(fslp=fslp)
        # ... your code here ...

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
```

## API Reference

### V4L2Fslp Class

**Constructor:**
```python
V4L2Fslp(portID=None, baudrate=None, **portargs)
```
- `portID`: Path to V4L2 subdev device (e.g., "/dev/v4l-subdev0"). If None, auto-detects.
- `baudrate`: Ignored (compatibility with I2CFslp)
- `**portargs`: Additional arguments (ignored for compatibility)

**Methods:**
- `sendFrame(channelID, data, dataSize)` - Send FSLP frame
- `readFrame(channelID, expectedReceiveBytes)` - Read FSLP response
- `sendToCamera(data, dataSize, expectedReceiveBytes)` - Send command and get response
- `pollDebug(channelID)` - Not implemented (returns empty)
- `dumpUnframed()` - Not implemented (returns empty)

### V4L2Port Class

**Constructor:**
```python
V4L2Port(device_path=None)
```

**Methods:**
- `open()` - Open V4L2 device
- `close()` - Close V4L2 device
- `isOpen()` - Check if device is open
- `find_flir_device()` - Auto-detect FLIR V4L2 device
- `ioctl_fslp_cmd(cmd_id, tx_data, rx_len)` - Send IOCTL command

## Device Detection

The module automatically searches for V4L2 subdev devices matching the pattern `/dev/v4l-subdev*`. You can also specify the device path explicitly:

```python
# Auto-detect
fslp = V4L2Fslp()

# Explicit device
fslp = V4L2Fslp(portID="/dev/v4l-subdev0")
```

## Error Handling

Common errors and solutions:

**"No FLIR Boson V4L2 device found"**
- Ensure the driver is loaded: `lsmod | grep flir_boson`
- Check for V4L2 devices: `ls -la /dev/v4l-subdev*`
- Verify device tree configuration

**"Permission denied"**
- Add user to video group: `sudo usermod -a -G video $USER`
- Or run with appropriate permissions

**"IOCTL failed"**
- Check dmesg for kernel driver messages
- Verify camera is powered and connected
- Ensure compatible driver version

## Comparison with I2CFslp

| Feature | I2CFslp | V4L2Fslp |
|---------|---------|----------|
| **Transport** | Direct I2C | V4L2 IOCTL |
| **Permissions** | I2C device access | V4L2 device access |
| **Concurrency** | Manual locking | Kernel managed |
| **Error Recovery** | Application handled | Driver handled |
| **Integration** | Hardware specific | Standard V4L2 |
| **Performance** | Direct hardware | Syscall overhead |

## Benefits

- **Kernel Integration**: Standard V4L2 subsystem integration
- **Better Error Handling**: Kernel driver provides robust error recovery
- **Concurrent Access**: Kernel manages multiple application access
- **Standard Interface**: Uses established V4L2 patterns
- **Drop-in Replacement**: Existing code works unchanged

## Limitations

- Requires V4L2 driver to be loaded and functional
- Small performance overhead compared to direct I2C
- Limited to FSLP commands supported by kernel driver

## Troubleshooting

1. **Check driver status:**
   ```bash
   lsmod | grep flir_boson
   dmesg | grep flir
   ```

2. **List V4L2 devices:**
   ```bash
   ls -la /dev/v4l-subdev*
   v4l2-ctl --list-devices
   ```

3. **Test device access:**
   ```bash
   python3 -c "from flir_boson_v4l2.python import V4L2Fslp; V4L2Fslp()"
   ```

4. **Run example:**
   ```bash
   cd flir_boson_v4l2/python
   python3 example.py
   ```

## See Also

- [Main driver README](../README.md)
- [Device tree bindings](../devicetree/flir-boson-dt.yaml)
- [FLIR SDK Documentation](../../SDK/SDK_Documentation/)