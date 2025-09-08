#!/usr/bin/env python3
"""FLIR Boson MIPI Control Script"""

import sys
import time
import argparse
from functools import wraps
from BosonSDK import *
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
    cam = CamAPI.pyClient(manualport=1, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")
    try:
        # Execute MIPI startup sequence
        timed_dvoSetMipiState = timing_wrapper("dvoSetMipiState")(cam.dvoSetMipiState)
        ret = timed_dvoSetMipiState(FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)

        timed_dvoSetType = timing_wrapper("dvoSetType")(cam.dvoSetType)
        ret = timed_dvoSetType(FLR_DVO_TYPE_E.FLR_DVO_TYPE_COLOR)

        timed_dvoSetOutputFormat = timing_wrapper("dvoSetOutputFormat")(cam.dvoSetOutputFormat)
        ret = timed_dvoSetOutputFormat(FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_YCBCR)

        timed_dvoSetOutputInterface = timing_wrapper("dvoSetOutputInterface")(cam.dvoSetOutputInterface)
        ret = timed_dvoSetOutputInterface(FLR_DVO_OUTPUT_INTERFACE_E.FLR_DVO_MIPI)

        ret = timed_dvoSetMipiState(FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)

        timed_dvoMuxSetType = timing_wrapper("dvoMuxSetType")(cam.dvoMuxSetType)
        ret = timed_dvoMuxSetType(FLR_DVOMUX_OUTPUT_IF_E.FLR_DVOMUX_OUTPUT_IF_MIPITX, FLR_DVOMUX_SOURCE_E.FLR_DVOMUX_SRC_IR, FLR_DVOMUX_TYPE_E.FLR_DVOMUX_TYPE_COLOR)

        timed_dvoSetMipiClockLaneMode = timing_wrapper("dvoSetMipiClockLaneMode")(cam.dvoSetMipiClockLaneMode)
        ret = timed_dvoSetMipiClockLaneMode(FLR_DVO_MIPI_CLOCK_LANE_MODE_E.FLR_DVO_MIPI_CLOCK_LANE_MODE_CONTINUOUS)

        # cam.telemetrySetState(FLR_ENABLE_E.FLR_DISABLE)
        # cam.telemetrySetPacking(FLR_TELEMETRY_PACKING_E.FLR_TELEMETRY_PACKING_DEFAULT)
        # cam.telemetrySetMipiEmbeddedDataTag(FLR_ENABLE_E.FLR_DISABLE)
        # cam.dvoSetOutputFormat(FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_YCBCR)
        # ret = cam.dvoSetType(FLR_DVO_TYPE_E.FLR_DVO_TYPE_COLOR)
        # print("dvoSetType returned:", ret)
        # ret = cam.dvoSetOutputFormat(FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_YCBCR)
        # print("dvoSetOutputFormat returned:", ret)
        # cam.dvoApplyCustomSettings()
        ret = timed_dvoSetMipiState(FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_ACTIVE)

        print("MIPI streaming started")
        return True

    except Exception as e:
        print(f"Error: {e}, traceback: {traceback.format_exc()}")
        return False
    finally:
        cam.Close()

def stop_mipi():
    """Stop MIPI streaming"""
    cam = CamAPI.pyClient(manualport=1, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")

    try:
        timed_dvoSetMipiState = timing_wrapper("dvoSetMipiState")(cam.dvoSetMipiState)
        ret = timed_dvoSetMipiState(FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)
        # cam.dvoSetMipiStartState(FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_ACTIVE)
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
    parser.add_argument('action', choices=['start', 'stop'], help='start or stop MIPI streaming')
    args = parser.parse_args()

    if args.action == 'start':
        success = start_mipi()
    else:
        success = stop_mipi()

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
