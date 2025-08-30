# FLIR Boson+ Hardware-Less Testing Framework

## Overview

This testing framework allows complete validation of the FLIR Boson+ V4L2 driver without requiring actual hardware. It provides:

- **100% SDK Protocol Compliance Validation**
- **Kernel Module Simulation Mode**
- **Hardware-less Driver Loading**
- **Complete FSLP Protocol Testing**

## Quick Start

### 1. Build for Simulation Mode

```bash
make clean && make simulation
# or: make sim
```

### 2. Run Complete Test Suite
```bash
sudo python3 test_simulation_mode.py
```

### 3. Build for Hardware Mode

```bash
make clean && make hardware
# or: make hw
```

## Test Components

### Python Protocol Validator (`test_fslp_validator.py`)

Validates FSLP protocol implementation against SDK specifications:
- ✅ I2C FSLP framing (magic tokens, big-endian length encoding)
- ✅ Command dispatcher (12-byte headers, sequence management)
- ✅ Parameter encoding (big-endian format)
- ✅ Response validation (sequence/command/status checks)
- ✅ Error handling verification

```bash
# Run standalone protocol validation
python3 test_fslp_validator.py

# Expected output:
# 🎉 ALL TESTS PASSED - FSLP Implementation is SDK Compliant!
```

### Simulation Mode Test Runner (`test_simulation_mode.py`)

Complete hardware-less testing framework:
- ✅ Kernel module loading/unloading
- ✅ Platform device registration
- ✅ V4L2 subdevice creation
- ✅ I2C simulation with printk logging
- ✅ Protocol compliance verification

```bash
# Run full simulation test suite
sudo python3 test_simulation_mode.py

# Unload module only
sudo python3 test_simulation_mode.py --unload

# Show help
sudo python3 test_simulation_mode.py --help
```

## Expected Test Output

### Protocol Validator
```
============================================================
FLIR Boson+ FSLP Protocol Validation Test Suite
============================================================
✅ Set Output Interface: PASSED
✅ Set DVO Type: PASSED
✅ Set MIPI State: PASSED
✅ Apply Settings: PASSED
✅ Get MIPI State: PASSED
✅ Sequence Management: PASSED

🎉 ALL TESTS PASSED - FSLP Implementation is SDK Compliant!
```

### Simulation Mode Runner
```
======================================================================
FLIR Boson+ Hardware-Less Simulation Test Suite
======================================================================
🔧 Test 1: Module Loading
✅ Simulation module loaded successfully

🔧 Test 2: Device Creation
✅ Platform device found: flir-boson-sim

🔧 Test 3: Kernel Log Activity
✅ Simulation activity detected in kernel logs

🔧 Test 4: FSLP Protocol Validation
✅ FSLP protocol validator: ALL TESTS PASSED

🎉 ALL SIMULATION TESTS PASSED!
```

## Monitoring Simulation Activity

### View Real-time Kernel Logs
```bash
# Monitor FSLP simulation traffic
sudo dmesg -w | grep -i "FSLP_SIM"

# Example output:
# [12345.678] flir-boson-sim flir-boson-sim: FSLP_SIM_TX: 20 bytes:
# [12345.679] FSLP_SIM_TX: 0000: 8E A1 00 10 00 00 00 01 00 06 00 07 FF FF FF FF
# [12345.680] FSLP_SIM_TX: 0010: 00 00 00 01
# [12345.681] flir-boson-sim flir-boson-sim: FSLP_SIM: Valid frame, payload_len=16
```

### Check Module Status
```bash
# Check if module is loaded
lsmod | grep flir_boson

# Check platform devices
ls /sys/bus/platform/devices/ | grep flir

# View recent kernel messages
dmesg | tail -20 | grep -i "flir\|boson"
```

## Protocol Validation Details

The validator generates exact protocol sequences and validates:

### I2C FSLP Frame Structure
```
Magic Tokens: [0x8E, 0xA1]
Length:       Big-endian u16 (payload only)
Payload:      Command dispatcher data
```

### Command Dispatcher Protocol
```
Sequence:     4 bytes (big-endian)
Function ID:  4 bytes (big-endian)
Status:       4 bytes (0xFFFFFFFF for commands)
Data:         Variable length
```

### Example Command Sequence
```
DVO_SET_MIPI_STATE(ACTIVE):
8EA1 0010 00000003 00060024 FFFFFFFF 00000002
│││   │     │        │        │        │
│││   │     │        │        │        └─ MIPI_STATE_ACTIVE (2)
│││   │     │        │        └───────── Status placeholder
│││   │     │        └─────────────────── DVO_SET_MIPI_STATE (0x00060024)
│││   │     └───────────────────────────── Sequence number (3)
│││   └─────────────────────────────────── Payload length (16 bytes)
│└└───────────────────────────────────────── Magic tokens (0x8E, 0xA1)
```

## Switching Modes

### Hardware Mode (Production)
```bash
make clean && make hardware
# or: make hw
```
- Uses real I2C communication with FLIR Boson+ camera
- Requires device tree configuration and hardware setup
- Production-ready for embedded systems

### Simulation Mode (Testing)
```bash
make clean && make simulation
# or: make sim
```
- Uses printk-based I2C simulation (no hardware required)
- Loads as platform device (no device tree needed)
- Perfect for protocol validation and development

### Check Current Build Mode
```bash
make status
# Output: "Current build: SIMULATION mode" or "Current build: HARDWARE mode"
```

## Troubleshooting

### Module Won't Load
```bash
# Check compilation errors
make clean && make

# Check kernel compatibility
uname -r
ls /lib/modules/$(uname -r)/build
```

### No Protocol Activity
```bash
# Verify simulation mode is enabled
grep -n "FLIR_SIMULATION_MODE" flir-boson.h

# Check for kernel messages
dmesg | grep -i "simulation\|flir"
```

### Permission Errors
```bash
# Ensure running as root
sudo python3 test_simulation_mode.py

# Check file permissions
ls -la *.ko *.py
```

## Integration with CI/CD

The testing framework can be integrated into automated testing:

```bash
#!/bin/bash
# CI/CD Integration Example

set -e

echo "Building FLIR Boson+ driver..."
make clean && make

echo "Running protocol validation..."
python3 test_fslp_validator.py

echo "Running simulation tests..."
sudo python3 test_simulation_mode.py

echo "All tests passed - ready for deployment!"
```

## Hardware Validation

After simulation testing passes:

1. **Disable simulation mode** in `flir-boson.h`
2. **Recompile** for hardware mode
3. **Connect FLIR Boson+ camera** via I2C/MIPI
4. **Load driver** with device tree configuration
5. **Test with V4L2 tools** (`v4l2-ctl`, `media-ctl`)

The simulation testing framework provides confidence that the protocol implementation is correct before hardware deployment.