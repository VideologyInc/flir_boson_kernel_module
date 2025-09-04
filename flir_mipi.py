#!/usr/bin/env python3
"""FLIR Boson MIPI Control Script"""

import sys
import time
import argparse
from BosonSDK import CamAPI
from BosonSDK.ClientFiles_Python import EnumTypes

def start_mipi():
    """Start MIPI streaming with YUV 4:2:2 configuration"""
    cam = CamAPI.pyClient(manualport=1, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")
    try:
        # Execute MIPI startup sequence
        ret = cam.dvoSetMipiState(EnumTypes.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)
        print("dvoSetMipiState returned:", ret)
        ret = cam.dvoSetType(EnumTypes.FLR_DVO_TYPE_E.FLR_DVO_TYPE_COLOR)
        print("dvoSetType returned:", ret)
        ret = cam.dvoSetOutputFormat(EnumTypes.FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_YCBCR)
        print("dvoSetOutputFormat returned:", ret)
        ret = cam.dvoSetOutputInterface(EnumTypes.FLR_DVO_OUTPUT_INTERFACE_E.FLR_DVO_MIPI)
        print("dvoSetOutputInterface returned:", ret)
        ret = cam.dvoSetMipiState(EnumTypes.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)
        print("dvoSetMipiState returned:", ret)
        ret = cam.dvoMuxSetType(EnumTypes.FLR_DVOMUX_OUTPUT_IF_E.FLR_DVOMUX_OUTPUT_IF_MIPITX, EnumTypes.FLR_DVOMUX_SOURCE_E.FLR_DVOMUX_SRC_IR, EnumTypes.FLR_DVOMUX_TYPE_E.FLR_DVOMUX_TYPE_COLOR)
        print("dvoSetMuxSetType returned:", ret)
        ret = cam.dvoSetMipiClockLaneMode(EnumTypes.FLR_DVO_MIPI_CLOCK_LANE_MODE_E.FLR_DVO_MIPI_CLOCK_LANE_MODE_CONTINUOUS)
        print("dvoSetMipiClockLaneMode returned:", ret)
        # cam.telemetrySetState(EnumTypes.FLR_ENABLE_E.FLR_DISABLE)
        # cam.telemetrySetPacking(EnumTypes.FLR_TELEMETRY_PACKING_E.FLR_TELEMETRY_PACKING_DEFAULT)
        # cam.telemetrySetMipiEmbeddedDataTag(EnumTypes.FLR_ENABLE_E.FLR_DISABLE)
        # cam.dvoSetOutputFormat(EnumTypes.FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_YCBCR)
        # ret = cam.dvoSetType(EnumTypes.FLR_DVO_TYPE_E.FLR_DVO_TYPE_COLOR)
        # print("dvoSetType returned:", ret)
        # ret = cam.dvoSetOutputFormat(EnumTypes.FLR_DVO_OUTPUT_FORMAT_E.FLR_DVO_YCBCR)
        # print("dvoSetOutputFormat returned:", ret)
        # cam.dvoApplyCustomSettings()
        cam.dvoSetMipiState(EnumTypes.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_ACTIVE)

        print("MIPI streaming started")
        return True

    except Exception as e:
        print(f"Error: {e}")
        return False
    finally:
        cam.Close()

def stop_mipi():
    """Stop MIPI streaming"""
    cam = CamAPI.pyClient(manualport=1, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")

    try:
        cam.dvoSetMipiState(EnumTypes.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_OFF)
        # cam.dvoSetMipiStartState(EnumTypes.FLR_DVO_MIPI_STATE_E.FLR_DVO_MIPI_STATE_ACTIVE)
        # cam.bosonWriteDynamicHeaderToFlash()

        print("MIPI streaming stopped")
        return True

    except Exception as e:
        print(f"Error: {e}")
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
