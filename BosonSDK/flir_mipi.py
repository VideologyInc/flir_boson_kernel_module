#!/usr/bin/env python3
"""FLIR Boson MIPI Control Script"""

import sys
import time
import argparse
from functools import wraps
from BosonSDK import EE, CamAPI, FLR_RESULT
import traceback


def timing_wrapper(func_name):
    """Decorator to time camera API function calls"""

    def decorator(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            start_time = time.time()
            result = func(*args, **kwargs)
            end_time = time.time()
            duration = (end_time - start_time) * 1000  # Convert to milliseconds
            print(f"{func_name} took {duration:.2f} ms, returned: {result}")
            return result

        return wrapper

    return decorator


def start_mipi():
    """Start MIPI streaming with YUV 4:2:2 configuration"""
    cam = CamAPI.pyClient(
        manualport=1, useI2C=True, peripheralAddress=0x6A, I2C_TYPE="smbus"
    )
    try:
        # Execute MIPI startup sequence

        ret = cam.dvoSetMipiState(EE.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)
        print("dvoSetMipiState returned:", ret)

        ret = cam.dvoSetType(EE.FLR_DVO_TYPE_E.FLR_DVO_TYPE_MONO14)
        print("dvoSetType returned:", ret)
        ret = cam.dvoSetOutputFormat(EE.FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_DEFAULT_FORMAT)
        print("dvoSetOutputFormat returned:", ret)
        ret = cam.dvoSetOutputInterface(EE.FLR_DVO_OUTPUT_INTERFACE_E.FLR_DVO_MIPI)
        print("dvoSetOutputInterface returned:", ret)
        ret = cam.dvoSetMipiState(EE.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)
        print("dvoSetMipiState returned:", ret)
        ret = cam.dvoMuxSetType(EE.FLR_DVOMUX_OUTPUT_IF_E.FLR_DVOMUX_OUTPUT_IF_MIPITX, EE.FLR_DVOMUX_SOURCE_E.FLR_DVOMUX_SRC_IR, EE.FLR_DVOMUX_TYPE_E.FLR_DVOMUX_TYPE_MONO16)

        print("dvoSetMuxSetType returned:", ret)
        ret = cam.dvoSetMipiClockLaneMode(EE.FLR_DVO_MIPI_CLOCK_LANE_MODE_E.FLR_DVO_MIPI_CLOCK_LANE_MODE_CONTINUOUS)
        print("dvoSetMipiClockLaneMode returned:", ret)
        # cam.telemetrySetState(EE.FLR_ENABLE_E.FLR_DISABLE)
        # cam.telemetrySetPacking(EE.FLR_TELEMETRY_PACKING_E.FLR_TELEMETRY_PACKING_DEFAULT)
        # cam.telemetrySetMipiEmbeddedDataTag(EE.FLR_ENABLE_E.FLR_DISABLE)
        # cam.dvoSetOutputFormat(EE.FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_YCBCR)
        # ret = cam.dvoSetType(EE.FLR_DVO_TYPE_E.FLR_DVO_TYPE_COLOR)

        ret = cam.dvoSetMipiState(EE.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_ACTIVE)

        print(f"MIPI streaming started: {ret}")
        return True
    except Exception as e:
        print(f"Error: {e}, traceback: {traceback.format_exc()}")
        return False
    finally:
        cam.Close()

def stop_mipi():
    """Stop MIPI streaming"""
    cam = CamAPI.pyClient(
        manualport=1, useI2C=True, peripheralAddress=0x6A, I2C_TYPE="smbus"
    )

    try:
        cam.dvoSetMipiStartState(EE.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)
        # cam.bosonWriteDynamicHeaderToFlash()

        print("MIPI streaming stopped")
        return True

    except Exception as e:
        print(f"Error: {e}, traceback: {traceback.format_exc()}")
        return False
    finally:
        cam.Close()


def main():
    parser = argparse.ArgumentParser(description="FLIR Boson MIPI Control")
    parser.add_argument(
        "action", choices=["start", "stop"], help="start or stop MIPI streaming"
    )
    args = parser.parse_args()

    if args.action == "start":
        success = start_mipi()
    else:
        success = stop_mipi()

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
