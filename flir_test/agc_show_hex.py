"""

From updated flir_boson device driver, run any v4l-ctl or gst command to get video streaming.
By dmesg we can get stored AGC (auto gain control) parameters of the camera.

But float numbers are in HEX format because of limitations of the Linux Kernel codes ;-)

This small program reads the output txt file from dmesg and convert HEX back to float to display the true AGC parameter values.

Copyright (C) 2026 Videology
Programmed by Jianping Ye <jye@videologyinc.com>

Feb 12. Added file parse to convert HEX to float and display them.

"""

import struct


def hex_to_float(hex_string):
    """
    Converts a raw hexadecimal string to a floating-point number.
    Handles both 32-bit (single precision) and 64-bit (double precision) floats.
    """
    # Remove any '0X' prefix if present
    if hex_string.startswith("0X"):
        hex_string = hex_string[2:]

    if len(hex_string) < 8:
        hex_string = hex_string.zfill(8)
    # print("for debug:  ", hex_string)

    # Determine precision based on the length of the hex string
    if len(hex_string) == 8:  # 32-bit float (4 bytes)
        format_code = "f"
    elif len(hex_string) == 16:  # 64-bit float (8 bytes)
        format_code = "d"
    else:
        raise ValueError(
            "Hex string must be 8 or 16 characters long for standard floats"
        )

    # Convert the hex string to bytes
    # The byte order ('endianness') might need specification (e.g., '!' for network byte order,
    # '<' for little-endian, '>' for big-endian). '!' is typically network big-endian.
    byte_data = bytes.fromhex(hex_string)

    # Unpack the bytes into a float
    # struct.unpack returns a tuple, so we take the first element [0]
    return struct.unpack(f"!{format_code}", byte_data)[0]


"""
# Example usage:
hex_32bit = '41973333' # Represents 18.899999618530273
hex_64bit = '4081637ef7d0424a' # Represents 520.456

print(f"'{hex_32bit}' as float: {hex_to_float(hex_32bit)}")
print(f"'{hex_64bit}' as float: {hex_to_float(hex_64bit)}")
"""


def read_agc_hex(name):
    with open(name) as fp:
        l = fp.readlines()
        for line in l:
            sl = line.strip().split("=")
            if len(sl) >= 2:
                # Convert and show hex in float
                hx = sl[1].strip()
                (
                    print(sl[0], "=", sl[1], f" = {hex_to_float(hx):.2f}")
                    if "0X" in sl[1]
                    else print(sl[0], "=", sl[1])
                )
            else:
                print(line)


read_agc_hex("boson_agc.txt")
