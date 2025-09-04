#!/usr/bin/env python3
import os
import argparse
from BosonSDK import CamAPI

def main():
    """Get FLIR Boson camera information including serial numbers and part number"""
    parser = argparse.ArgumentParser(description="Get FLIR Boson camera information")
    parser.add_argument('--port', '-p', type=int, default=1, help='I2C port number (default: 1)')
    args = parser.parse_args()

    myCam = CamAPI.pyClient(manualport=args.port, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")

    try:
        # myCam.fslp.port.read(100)
        # myCam.fslp.port.read(10)

        result1, cam_sernum = myCam.bosonGetCameraSN()
        result2, sensor_sernum = myCam.bosonGetSensorSN()
        result3, partnum_object = myCam.bosonGetCameraPN()

        part_number = "".join(chr(char) for char in partnum_object.value)

        print(" ")
        print("--------------------------------------------------------------")

        print(f"The serial number of this camera is {hex(cam_sernum)}")
        print(f"The serial number of this camera's sensor is {hex(sensor_sernum)}")
        print(f"The part number of this camera is {part_number}")

        print("--------------------------------------------------------------")
        print(" ")

    finally:
        myCam.Close()

if __name__ == "__main__":
    main()
