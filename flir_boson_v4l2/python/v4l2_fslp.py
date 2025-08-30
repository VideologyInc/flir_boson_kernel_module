# -*- coding: utf-8 -*-
"""
FSLP communication via V4L2 driver IOCTL interface
Drop-in replacement for I2CFslp
"""

import os
import fcntl
import struct
import glob
from struct import pack, unpack

# Import FLIR SDK base class
import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '../../SDK/CommunicationFiles'))
from FslpBase import FslpBase

MAGIC_TOKEN = bytearray([0x8E, 0xA1])
LEN_HEADER = 4


class V4L2Port:
    """V4L2 subdev port interface"""

    def __init__(self, device_path=None):
        self.device_path = device_path
        self.fd = None
        self._is_open = False

    def find_flir_device(self):
        """Find FLIR Boson V4L2 subdev device"""
        subdev_pattern = "/dev/v4l-subdev*"
        devices = glob.glob(subdev_pattern)

        for device in devices:
            try:
                with open(device, 'rb'):
                    return device
            except (OSError, IOError):
                continue

        return None

    def open(self):
        """Open V4L2 device"""
        if self._is_open:
            return

        if not self.device_path:
            self.device_path = self.find_flir_device()

        if not self.device_path:
            raise Exception("No FLIR Boson V4L2 device found")

        try:
            self.fd = os.open(self.device_path, os.O_RDWR)
            self._is_open = True
        except OSError as e:
            raise Exception(f"Failed to open V4L2 device {self.device_path}: {e}")

    def close(self):
        """Close V4L2 device"""
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
        self._is_open = False

    def isOpen(self):
        """Check if device is open"""
        return self._is_open

    def write(self, data):
        """Send FSLP frame via driver IOCTL"""
        if not self._is_open:
            raise Exception("Device not open")

        # IOCTL: _IOWR('F', 0x01, struct flir_boson_ioctl_fslp)
        # struct: tx_len(4) + rx_len(4) + data(256) = 264 bytes
        IOC_INOUT = 3
        IOCTL_CMD = (IOC_INOUT << 30) | (ord('F') << 8) | (0x01 << 0) | (264 << 16)

        tx_len = len(data)
        if tx_len > 256:
            raise ValueError("Data too large (max 256 bytes)")

        # Pack: tx_len (4), rx_len (4), data (256)
        ioctl_data = struct.pack('<II', tx_len, 0)
        ioctl_data += data.ljust(256, b'\x00')

        try:
            fcntl.ioctl(self.fd, IOCTL_CMD, ioctl_data)
        except OSError as e:
            raise Exception(f"IOCTL write failed: {e}")

    def read(self, length):
        """Read FSLP frame via driver IOCTL"""
        if not self._is_open:
            raise Exception("Device not open")

        # IOCTL: _IOWR('F', 0x01, struct flir_boson_ioctl_fslp)
        IOC_INOUT = 3
        IOCTL_CMD = (IOC_INOUT << 30) | (ord('F') << 8) | (0x01 << 0) | (264 << 16)

        if length > 256:
            raise ValueError("Read length too large (max 256 bytes)")

        # Pack: tx_len (4), rx_len (4), data (256)
        ioctl_data = struct.pack('<II', 0, length)
        ioctl_data += b'\x00' * 256

        try:
            result = fcntl.ioctl(self.fd, IOCTL_CMD, ioctl_data)
            # Extract response data starting at offset 8 (after tx_len, rx_len)
            return result[8:8+length]
        except OSError as e:
            raise Exception(f"IOCTL read failed: {e}")


class V4L2Fslp(FslpBase):
    """FSLP implementation using V4L2 driver - matches I2CFslp interface"""

    def __init__(self, portID=None, baudrate=None, **portargs):
        # portID is device path for V4L2, baudrate ignored
        port = V4L2Port(portID)
        super().__init__(port)
        self.port.open()

    def sendFrame(self, channelID, data, dataSize):
        """Send FSLP frame - matches I2CFslp.sendFrame()"""
        if not self.port.isOpen():
            raise Exception("V4L2 port is not open")

        # Build FSLP frame exactly like I2CFslp
        sendBuffer = MAGIC_TOKEN[:]
        sendBuffer.extend(pack(">H", dataSize))
        sendBuffer.extend(data)

        self.port.write(sendBuffer)

    def readFrame(self, channelID, expectedReceiveBytes):
        """Read FSLP frame - matches I2CFslp.readFrame()"""
        if not self.port.isOpen():
            raise Exception("Port is not open")

        # Read FSLP header
        receiveBuffer = self.port.read(4)

        # Validate magic token
        if receiveBuffer[0:2] != MAGIC_TOKEN[0:2]:
            raise ValueError("Did not receive MAGIC_TOKEN")

        # Get data length
        toRead = unpack(">H", receiveBuffer[2:])[0]
        if toRead != expectedReceiveBytes:
            print("WARNING MSG declared {:d} bytes but {:d} expected)".format(toRead, expectedReceiveBytes))

        # Read data
        receiveBuffer = self.port.read(toRead)
        return receiveBuffer