from BosonSDK import *

myCam = CamAPI.pyClient(manualport=1, useI2C=True, peripheralAddress=0x6a, I2C_TYPE="smbus")

returnCode, major, minor, patch = myCam.bosonGetSoftwareRev()
print('Software Version: %d %d %d' % (major,minor,patch))

result, serialnumber = myCam.bosonGetCameraSN()
print('camera SN: %d' % serialnumber)

print()
print('dvoGetOutputInterface:')
print(myCam.dvoGetOutputInterface())

print()
print('dvoGetType:')
print(myCam.dvoGetType())

print()
print('dvoGetOutputFormat: ')
print(myCam.dvoGetOutputFormat())

print()
print('dvoGetOutputIr16Format: ')
print(myCam.dvoGetOutputIr16Format())

print()
print('dvoGetMipiStartState: ')
print(myCam.dvoGetMipiStartState())

print()
print('dvoGetMipiState: ')
print(myCam.dvoGetMipiState())

print()
print('dvoGetLCDConfig: ')
print(myCam.dvoGetLCDConfig())

#returnCode, horizontalSyncWidth, verticalSyncWidth, clocksPerRowPeriod, horizontalFrontPorch, horizontalBackPorch, frontTelemetryPixels, rearTelemetryPixels, videoColumns, validColumns, telemetryRows, videoRows, validRows, verticalFrontPorch, verticalBackPorch, rowPeriodsPerFrame, clocksPerFrame, clockRateInMHz, frameRateInHz, validOnRisingEdge, dataWidthInBits 
print()
print('dvoGetClockInfo: ')
print(myCam.dvoGetClockInfo())
print()

#print('ClockInfo:')
#print(returnCode)
#print(frameRateInHz)
#print(clockRateInMHz)
#print(dataWidthInBits)

myCam.Close()

