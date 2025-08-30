# FLIR Boson+ MIPI V4L2 Driver

A Linux V4L2 driver for FLIR Boson and Boson+ thermal imaging cameras with MIPI CSI-2 interface.

## Features

- V4L2 subdevice interface for standard video operations
- Support for RAW8, RAW14, and UYVY video formats
- Multiple resolution support (320x256, 640x512)
- Frame rate control (30fps, 60fps)
- IOCTL interface for full FLIR SDK access
- Device tree integration
- Power management with GPIO reset control

## Supported Hardware

- FLIR Boson 320x256 with MIPI interface
- FLIR Boson+ 640x512 with MIPI interface
- Compatible with any system supporting MIPI CSI-2

## Device Tree Configuration

```dts
&i2c1 {
    flir_boson: camera@6a {
        compatible = "flir,boson-mipi";
        reg = <0x6a>;
        reset-gpios = <&gpio1 5 GPIO_ACTIVE_LOW>;

        port {
            flir_boson_ep: endpoint {
                remote-endpoint = <&csi2_ep>;
                data-lanes = <1 2>;
                clock-lanes = <0>;
                clock-noncontinuous;
            };
        };
    };
};
```

## Driver Architecture

### Core Components

- **flir-boson-core.c**: Main V4L2 subdev implementation
- **flir-boson-fslp.c**: FLIR Serial Line Protocol over I2C
- **flir-boson.h**: Header definitions and data structures

### V4L2 Operations

| Operation | Description |
|-----------|-------------|
| `enum_mbus_code` | Lists supported formats (RAW8, RAW14, UYVY) |
| `enum_frame_size` | Lists supported resolutions |
| `enum_frame_interval` | Lists supported frame rates |
| `get_fmt/set_fmt` | Format control and switching |
| `s_stream` | Start/stop MIPI transmission |
| `s_power` | Power management |

### IOCTL Interface

For advanced features not covered by V4L2, the driver provides direct FSLP access:

```c
struct flir_boson_ioctl_fslp {
    u32 cmd_id;     // FLIR function code
    u32 tx_len;     // TX data length
    u32 rx_len;     // RX data length
    u8 data[256];   // TX/RX buffer
};
```

### Format Mapping

| V4L2 Format | FLIR Type | Description |
|-------------|-----------|-------------|
| `MEDIA_BUS_FMT_SBGGR8_1X8` | `MONO8` | 8-bit post-AGC monochrome |
| `MEDIA_BUS_FMT_SBGGR14_1X14` | `MONO14` | 14-bit pre-AGC radiometric |
| `MEDIA_BUS_FMT_UYVY8_1X16` | `COLOR` | 16-bit YUV 4:2:2 color |

## Building

### In-tree Build

1. Copy driver to kernel source tree:
   ```bash
   cp -r flir_boson_v4l2 $KERNEL_SRC/drivers/media/i2c/
   ```

2. Add to parent Kconfig:
   ```kconfig
   source "drivers/media/i2c/flir_boson_v4l2/Kconfig"
   ```

3. Add to parent Makefile:
   ```makefile
   obj-y += flir_boson_v4l2/
   ```

4. Configure and build:
   ```bash
   make menuconfig  # Enable CONFIG_VIDEO_FLIR_BOSON
   make modules
   ```

### Out-of-tree Build

```bash
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

## Usage Example

### Basic V4L2 Operations

```bash
# List formats
v4l2-ctl --device /dev/v4l-subdev0 --list-formats-ext

# Set format to RAW14 640x512
v4l2-ctl --device /dev/v4l-subdev0 --set-fmt-video \
    width=640,height=512,pixelformat=RW14

# Start streaming
media-ctl --device /dev/media0 --set-v4l2 '"flir-boson":0[fmt:RW14/640x512]'
```

### SDK Access via IOCTL

Use the existing FLIR SDK with minimal modifications - the driver provides transparent FSLP access for all advanced thermal imaging features.

## Power Management

The driver implements standard Linux power management:

- Automatic power-on during probe
- 2.5 second boot delay (as per FLIR specification)
- GPIO reset control
- Runtime power management support

## MIPI State Machine

The driver manages FLIR's MIPI state machine:

- **OFF**: MIPI transmitter disconnected
- **PAUSED**: MIPI signals in LP11 state
- **ACTIVE**: Full MIPI transmission

## Limitations

This driver provides essential V4L2 operations only. Advanced thermal imaging features (AGC, radiometry, symbology, etc.) should be accessed via the existing FLIR SDK through the IOCTL interface.

## License

GPL-2.0 - See COPYING for details.