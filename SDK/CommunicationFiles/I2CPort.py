# -*- coding: utf-8 -*-
"""
I2C port supported by custom library
"""

from .PortBase import PortBase
I2C_AARDVARK_AVAILABLE = False
AARDVARK_ERROR_STRING = "TotalPhase Aardvark driver not available. (pip install aardvark-py)"
SMBUS_AVAILABLE = False
SMBUS_ERROR_STRING = "SMBus not available. (pip install smbus2) "
I2CDRIVER_AVAILABLE = False
I2CDRIVER_ERROR_STRING = "I2CDriver support not available. (missing i2cdriver_modified and/or pyserial?) "

try:
    from aardvark_py import *
    import array as arr
    I2C_AARDVARK_AVAILABLE = True
except ImportError:
    pass
    #print(AARDVARK_ERROR_STRING)

try:
    from .i2cdriver_modified import I2CDriver
    READ = 1
    WRITE = 0
    # `pip install i2cdriver` for unmodified code
    # will need to remove/modify __del__ and close() apis 
    # requires "import serial" to work (pip install pyserial?)
    I2CDRIVER_AVAILABLE = True
except ImportError:
    pass
    #print(I2CDRIVER_ERROR_STRING)

try:
    from smbus2 import SMBus, i2c_msg
    SMBUS_AVAILABLE = True
except ImportError:
    pass
    #print(SMBUS_ERROR_STRING)


class I2CSMBusPort(PortBase):
    def __init__(self, portID=2, baudrate=400000, peripheralAddress=0x6A):
        '''
        portID = i2c bus number (e.g. 0 or 1) or an absolute file path (e.g. `/dev/i2c-42`).
        '''
        #SMBus port 2 for Tx2 adapter. 
        if portID is None:
            portID = 2
        if baudrate is None:
            baudrate = 400000
        super().__init__(portID, int(baudrate))
        self.peripheralAddress = peripheralAddress
        self.portOpen = False
        if not SMBUS_AVAILABLE:
            raise ImportError(SMBUS_ERROR_STRING)
        if 400000 != baudrate:
            print("Warning smbus2 cannot directly change I2C clock rate, change device settings directly")
        self.i2c_msg = i2c_msg()

    def open(self):
        self.port = SMBus(self.portID)
        self.portOpen = True

    def close(self):
        try:
            if self.isOpen():
                self.port.close()
        finally:
            self.portOpen = False

    def isOpen(self):
        return self.portOpen

    def isAvailable(self):
        return None

    def write(self, data):
        msg = self.i2c_msg.write(self.peripheralAddress, data)
        self.port.i2c_rdwr(msg)

    def read(self, numberOfBytes):
        msg = self.i2c_msg.read(self.peripheralAddress, numberOfBytes)
        self.port.i2c_rdwr(msg)
        if msg.len != numberOfBytes:
            raise IOError(f"Read incorrect number of bytes. Got {msg.len} instead of {numberOfBytes}")
        return bytearray(msg.buf[0:msg.len])

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

class I2CDriverPort(PortBase):
    def __init__(self, portID='COM<N>', baudrate=400000, peripheralAddress=0x6A):
        '''
        portID = Serial Port name for I2CDriver/I2CMini pod (e.g. 'COM15' or '/dev/ttyUSB4').
        '''
        if not I2CDRIVER_AVAILABLE:
            raise ImportError(I2CDRIVER_ERROR_STRING)
        
        if baudrate is None:
            baudrate = 400000
        super().__init__(portID, int(baudrate))
        self.peripheralAddress = 0x6A
        self.portOpen = False

    def open(self):
        try:
            self.port = I2CDriver(self.portID, reset=True)
        except Exception:
            self.portOpen = False
            del self
            raise IOError("I2CDriver failed to initialize")
        self.port.setspeed(int(self.baudrate/1000))
        connected_peripherals = []
        try:
            connected_peripherals = self.port.scan(silent=True)
        except Exception: # actually a SerialException
            self.close()
            raise IOError("I2C Address scan failed. Check bus connections and retry")
        
        if self.peripheralAddress not in connected_peripherals:
            connected_peripherals = " ".join([f"0x{by:02X}" for by in connected_peripherals])
            raise IOError(f"Selected peripheral address 0x{self.peripheralAddress:02X} not present!\n\tOptions:{connected_peripherals}")
        self.portOpen = True

    def close(self):
        try:
            self.port.close() #added to _modified library
            if self.isOpen():
                del self.port
        except AttributeError:
            pass
        self.portOpen = False

    def isOpen(self):
        return self.portOpen

    def isAvailable(self):
        return None

    def write(self, data):
        self.port.start(self.peripheralAddress, WRITE)
        receiveBuffer = self.port.write(data)
        self.port.stop()

    def read(self, numberOfBytes):
        self.port.start(self.peripheralAddress, READ)
        receiveBuffer = self.port.read(numberOfBytes)
        self.port.stop()
        if len(receiveBuffer) != numberOfBytes:
            raise IOError(f"Read incorrect number of bytes. Got {len(receiveBuffer)} instead of {numberOfBytes}")
        return bytearray(receiveBuffer)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

class I2CAardvarkPort(PortBase):
    def __init__(self, portID=0, baudrate=400000, peripheralAddress=0x6A):
        '''
        portID = Index of TotalPhase Aardvark pod (default=0).
        '''
        if portID is None:
            portID = 0
        if baudrate is None:
            baudrate = 400000
        super().__init__(portID, int(baudrate))
        self.busTimeout = 450 #ms - 450 is maximum
        self.peripheralAddress = peripheralAddress
        self.portOpen = False
        if not I2C_AARDVARK_AVAILABLE:
            raise ImportError(AARDVARK_ERROR_STRING)

    def open(self):
        self.port = aa_open(self.portID)
        if self.port <= 0:
            raise IOError("Failed to open I2C port {:d}!".format(self.portID))

        # Ensure that the I2C subsystem is enabled
        aa_configure(self.port, AA_CONFIG_SPI_I2C)

        # Disable the I2C bus pullup resistors (2.2k resistors).
        # This command is only effective on v2.0 hardware or greater.
        # The pullup resistors on the v1.02 hardware are enabled by default.
        aa_i2c_pullup(self.port, AA_I2C_PULLUP_NONE)

        # Disable target power.
        # This command is only effective on v2.0 hardware or greater.
        # The power pins on the v1.02 hardware are not enabled by default.
        aa_target_power(self.port, AA_TARGET_POWER_NONE)

        # Set the speed in khz
        bitrate = aa_i2c_bitrate(self.port, int(self.baudrate/1000))

        # Set the bus lock timeout
        bus_timeout = aa_i2c_bus_timeout(self.port, self.busTimeout)

        self.portOpen = True

    def close(self):
        try:
            if self.isOpen():
                aa_close(self.port)
        finally:
            self.portOpen = False

    def isOpen(self):
        return self.portOpen

    def isAvailable(self):
        return None

    def write(self, data):
        data = arr.array('B', data)
        bytesCount = aa_i2c_write(self.port, self.peripheralAddress, AA_I2C_NO_FLAGS, data)
        if bytesCount < 0:
            raise IOError("Failed to write to Aardvark device {:d}!".format(self.portID))
        elif bytesCount != len(data):
            raise IOError("Failed to write to Aardvark device {:d}! Write {:d} bytes but {:d} expected".format(self.portID, bytesCount, len(data)))

    def read(self, numberOfBytes):
        retval, receiveBuffer, receiveBytes = aa_i2c_read_ext(self.port,  self.peripheralAddress, AA_I2C_NO_FLAGS, numberOfBytes)
        if retval < 0:
            raise IOError("Failed to read from Aardvark device {:d}!".format(self.portID))
        elif receiveBytes != numberOfBytes:
            raise IOError("Failed to read from Aardvark device {:d}! Read {:d} bytes but {:d} expected".format(self.portID, receiveBytes, numberOfBytes))
        return bytearray(receiveBuffer)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
