import os
from BosonSDK import CamAPI

myCam = CamAPI.pyClient(manualport=1, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")

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

myCam.Close()
