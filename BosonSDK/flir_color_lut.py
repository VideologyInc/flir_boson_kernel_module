#!/usr/bin/env python3
"""FLIR Boson MIPI Control Script"""

import sys
import argparse
import traceback

from BosonSDK import CamAPI
from BosonSDK.ClientFiles_Python import EnumTypes

# LUT name to ID mapping
lut_names = {
    'whitehot': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_WHITEHOT,
    'default': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_DEFAULT,
    'blackhot': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_BLACKHOT,
    'rainbow': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_RAINBOW,
    'rainbow_hc': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_RAINBOW_HC,
    'ironbow': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_IRONBOW,
    'lava': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_LAVA,
    'arctic': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_ARCTIC,
    'globow': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_GLOBOW,
    'gradedfire': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_GRADEDFIRE,
    'hottest': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_HOTTEST,
    'emberglow': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_EMBERGLOW,
    'aurora': EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_AURORA
}

def set_color_lut(lut_id, port=1):
    """Set color lookup table for thermal imaging"""

    cam = CamAPI.pyClient(manualport=port, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")

    try:
        # Convert string input to enum value
        if isinstance(lut_id, str):
            if lut_id.lower() in lut_names:
                lut_enum = lut_names[lut_id.lower()]
                lut_name = lut_id.lower()
            else:
                raise ValueError(f"Unknown LUT name: {lut_id}")
        else:
            # Numeric input
            lut_id = int(lut_id)
            if 0 <= lut_id < EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_ID_END:
                lut_enum = EnumTypes.FLR_COLORLUT_ID_E(lut_id)
                # Find name for numeric ID
                lut_name = next((name for name, enum_val in lut_names.items() if enum_val == lut_id), f"LUT_{lut_id}")
            else:
                raise ValueError(f"LUT ID must be between 0 and {EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_ID_END-1}")

        # Set the color LUT
        ret = cam.colorLutSetId(lut_enum)
        print(f"Color LUT set to: {lut_name} (ID: {lut_enum})")
        return ret

    except Exception as e:
        print(f"Error setting color LUT: {e}, traceback: {traceback.format_exc()}")
        return False
    finally:
        cam.Close()

def get_color_lut(port=1):
    """Get current color lookup table setting"""
    cam = CamAPI.pyClient(manualport=port, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")

    try:
        ret, current_lut = cam.colorLutGetId()
        rev = {v: k for k, v in lut_names.items()}
        print(f"Current color LUT ID: {current_lut}, {rev[current_lut]}")
        return current_lut

    except Exception as e:
        print(f"Error getting color LUT: {e}, traceback: {traceback.format_exc()}")
        return None
    finally:
        cam.Close()

def main():
    parser = argparse.ArgumentParser(description="FLIR Boson MIPI color LUT mode config")
    parser.add_argument('-p', '--port', type=int, default=1, help='I2C port to use (default: 1)')
    subparsers = parser.add_subparsers(dest="action", required=True)

    set_parser = subparsers.add_parser("set", help='Set color LUT')
    set_parser.add_argument('lut_id', choices=list(lut_names.keys()) + [str(i) for i in range(EnumTypes.FLR_COLORLUT_ID_E.FLR_COLORLUT_ID_END)])

    get_parser = subparsers.add_parser("get", help="Get the current value")

    args = parser.parse_args()

    success = 0

    if args.action == 'set':
        success = set_color_lut(args.lut_id, args.port)
    elif args.action == 'get':
        result = get_color_lut(args.port)
        success = 0 if result is not None else 1
    else:
        parser.print_help()
        sys.exit(1)

    sys.exit(success)

if __name__ == "__main__":
    main()
