# -*- coding: utf-8 -*-
"""
FSLP communication by I2C port - custom library is used
"""

from .FslpBase import FslpBase
from .I2CPort import I2CAardvarkPort, I2CDriverPort, I2CSMBusPort
from struct import pack, unpack

MAGIC_TOKEN = bytearray( [0x8E, 0xA1])
LEN_HEADER = 4


class I2CFslp(FslpBase):
    def __init__(self, portID=None, baudrate=None, **portargs):
        try:
            I2C_TYPE = portargs['I2C_TYPE']
        except KeyError:
            I2C_TYPE = "AARDVARK"
        try:
            peripheralAddress=portargs['peripheralAddress']
        except KeyError:
            peripheralAddress=0x6A
        if "SMBUS" in I2C_TYPE.upper():
            super().__init__(I2CSMBusPort(portID=portID, baudrate=baudrate, peripheralAddress=peripheralAddress))
        elif "AARD" in I2C_TYPE:
            super().__init__(I2CAardvarkPort(portID=portID, baudrate=baudrate, peripheralAddress=peripheralAddress))
        else:
            super().__init__(I2CDriverPort(portID=portID, baudrate=baudrate, peripheralAddress=peripheralAddress))
    
    def sendFrame(self, channelID, data, dataSize):
        if not self.port.isOpen():
            raise Exception("I2C port is not open")
        #I2C FSLP-like header
        # shallow copy token
        sendBuffer = MAGIC_TOKEN[:] 
        # declare big endian u16 data length
        sendBuffer.extend(pack(">H",dataSize))
        # add data
        sendBuffer.extend(data)
        
        self.port.write(sendBuffer)

    def readFrame(self, channelID, expectedReceiveBytes):
        if not self.port.isOpen():
            raise Exception("Port is not open")
        #I2C FSLP-like header
        receiveBuffer = self.port.read(4)
        # evaluate token
        if receiveBuffer[0:2] != MAGIC_TOKEN[0:2]:
            raise ValueError("Did not receive MAGIC_TOKEN")
        # determine big endian u16 data length
        toRead = unpack(">H",receiveBuffer[2:])[0]
        if toRead != expectedReceiveBytes:
            print("WARNING MSG declared {:d} bytes but {:d} expected)".format(toRead, expectedReceiveBytes))
        
        receiveBuffer = self.port.read(toRead)
        return receiveBuffer
