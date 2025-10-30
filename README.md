# FLIR Boson+ MIPI V4L2 Driver

A Linux V4L2 driver for FLIR Boson and Boson+ thermal imaging cameras with MIPI CSI-2 interface. Specifically for use with Scailx imx8mp. Driver might need to be modified for other platforms.
Note that kernel patches are aplied to make the driver work in Y14 mode. See the [Yocto recipe](https://github.com/VideologyInc/meta-scailx-flir/blob/master/meta-scailx-flir/recipes-kernel/linux/linux-imx_6.6%25.bbappend) for the patch details.

## Features

- V4L2 subdevice interface for standard video operations
- Support for RAW8, RAW14, and UYVY video formats
- Multiple resolution support (320x256, 640x512, and 640x514 for Telemetry lines)
- Device tree integration
- Power management with GPIO reset control

## Supported Hardware

- FLIR Boson+ 640x512 with MIPI interface
- Compatible with any system supporting MIPI CSI-2
- Scailx Mipi Boson Adapter board

## Usage

Refer to the [notebook here](flir_v4l_capture_tests.ipynb) for detailed usage instructions with the FLIR SDK.

### Core Components

- **flir-boson-core.c**: Main V4L2 subdev implementation
- **flir-boson-fslp.c**: FLIR Serial Line Protocol over I2C
- **flir-boson.h**: Header definitions and data structures

### Format Mapping

| V4L2 Format                  | FLIR Type | FourCC | Gstreamer 'format=' | Description                |
| ---------------------------- | --------- | ------ | ------------------- | -------------------------- |
| `MEDIA_BUS_FMT_SBGGR8_1X8`   | `MONO8`   | 'GREY' | GRAY8               | 8-bit post-AGC monochrome  |
| `MEDIA_BUS_FMT_SBGGR14_1X14` | `MONO14`  | 'Y16 ' | GRAY16_LE           | 14-bit pre-AGC radiometric |
| `MEDIA_BUS_FMT_UYVY8_1X16`   | `COLOR`   | 'YUYV' | NV12                | 16-bit YUV 4:2:2 color     |

### SDK Access via IOCTL

The driver provides two ways to access the full FLIR SDK:

#### 1. Python Interface (Recommended)

Use the included Python interface can be used for userspace interaction with the FLir boson device via the I2C bus and the Bosn SDK. See [flir_get_info.py](BosonSDK/flir_get_info.py) for example usage.

```python
from BosonSDK import CamAPI

myCam = CamAPI.pyClient(manualport=args.port, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")
res, cam_sernum = myCam.bosonGetCameraSN()
print("Camera Serial Number: ", cam_sernum)
```

## License

MIT for kernel driver, "BOSON TOOLS and SDK License Agreement" for Boson Python SDK. See LICENSE.txt for details.