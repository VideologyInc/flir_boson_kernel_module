// SPDX-License-Identifier: GPL-2.0
/*
 * FLIR Boson+ FSLP Communication Layer - SDK Compliant Implementation
 * Copyright (C) 2024
 *
 * This implementation follows the exact SDK architecture:
 * - Layer 1: I2C FSLP Framing (matches I2CFslp.py)
 * - Layer 2: Command Dispatcher (matches Client_Dispatcher.py/c)
 * - Layer 3: Command Packagers (matches Client_Packager.py/c)
 */

#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h>
#include "flir-boson.h"
#include "ReturnCodes.h"
#include "Client_Dispatcher.h"
#include "I2C_Connector.h"

/* ========================================================================
 * Layer 0: Raw I2C Transport
 * ======================================================================== */

/* u32oBytes - copied from SDK/ClientFiles_MSVC/Serializer_BuiltIn.c */
static void u32oBytes(u32 inVal, u8 *outBuff)
{
	u8 *outPtr = outBuff;
	*outPtr++ = (u8)(inVal >> 24 & 0xff);
	*outPtr++ = (u8)(inVal >> 16 & 0xff);
	*outPtr++ = (u8)(inVal >> 8 & 0xff);
	*outPtr = (u8)(inVal & 0xff);
}

/* byteToUINT32 - extract big-endian uint32 from buffer */
static u32 byteToUINT32(const u8 *inBuff)
{
	return (inBuff[0] << 24) | (inBuff[1] << 16) | (inBuff[2] << 8) | inBuff[3];
}

/* ========================================================================
 * Layer 3: Command Packagers (matches Client_Packager.py/c patterns)
 * ======================================================================== */

FLR_RESULT flir_boson_send_int_cmd(struct flir_boson_dev *sensor, FLR_FUNCTION cmd, u32 val)
{
	u8 send_data[4];
	u8 recv_data[4];
	u32 recv_bytes = 1;
	u32 seq_num = ++sensor->command_count;
	FLR_RESULT ret;

	u32oBytes(val, send_data);

	ret = CLIENT_dispatcher(sensor, seq_num, cmd, send_data, sizeof(send_data), recv_data, &recv_bytes);
	if (ret != R_SUCCESS) {
        dev_err(sensor->dev, "%s: failed: cmd: %08x, res=%s", __FUNCTION__, cmd, flr_result_to_string(ret));
    } else {
        dev_dbg(sensor->dev, "%s: sent cmd: %08x", __FUNCTION__, cmd);
    }
	return ret;
}

FLR_RESULT flir_boson_get_int_val(struct flir_boson_dev *sensor, FLR_FUNCTION cmd, u32 *val)
{
	u8 receive_data[4];
	u32 receive_bytes = sizeof(receive_data);
	u32 seq_num = ++sensor->command_count;
	FLR_RESULT ret;

	ret = CLIENT_dispatcher(sensor, seq_num, cmd, NULL, 0, receive_data, &receive_bytes);
	if (ret == R_SUCCESS && receive_bytes >= 4) {
		*val = byteToUINT32(receive_data);
	} else {
        dev_err(sensor->dev, "%s: failed: %s", __FUNCTION__, flr_result_to_string(ret));
    }

	return ret;
}


FLR_RESULT flir_boson_set_dvo_muxtype(struct flir_boson_dev *sensor, FLR_DVOMUX_OUTPUT_IF_E output, FLR_DVOMUX_SOURCE_E source, FLR_DVOMUX_TYPE_E type) {
    // Allocate buffers with space for marshalled data
    u32 sendBytes = 12;
    u8 sendData[12];
    u32 receiveBytes = 1;
    u8 receiveData[1];
    u8 *outPtr = (u8 *)sendData;
    u32 seq_num = ++sensor->command_count;

    //write output to sendData buffer
    { //Block to allow reuse of inVal
        if(outPtr >= (sendData+sendBytes))
            return R_SDK_PKG_BUFFER_OVERFLOW;
        FLR_DVOMUX_OUTPUT_IF_E inVal = output;
        u32oBytes(inVal, (u8 *) outPtr);
        outPtr += 4;
    }

    //write source to sendData buffer
    { //Block to allow reuse of inVal
        if(outPtr >= (sendData+sendBytes))
            return R_SDK_PKG_BUFFER_OVERFLOW;
        FLR_DVOMUX_SOURCE_E inVal = source;
        u32oBytes(inVal, (u8 *) outPtr);
        outPtr += 4;
    }

    //write type to sendData buffer
    { //Block to allow reuse of inVal
        if(outPtr >= (sendData+sendBytes))
            return R_SDK_PKG_BUFFER_OVERFLOW;
        FLR_DVOMUX_TYPE_E inVal = type;
        u32oBytes(inVal, (u8 *) outPtr);
        outPtr += 4;
    }

    FLR_RESULT returncode = CLIENT_dispatcher(sensor, seq_num, DVOMUX_SETTYPE, sendData, sendBytes, receiveData, &receiveBytes);
    if((u32) returncode)
        return returncode;

    return R_SUCCESS;

}// End of CLIENT_pkgDvomuxSetType()

// Synchronous (potentially MultiService incompatible) transmit+receive variant
FLR_RESULT flir_boson_get_dvo_muxtype(struct flir_boson_dev *sensor, FLR_DVOMUX_OUTPUT_IF_E output, FLR_DVOMUX_SOURCE_E *source, FLR_DVOMUX_TYPE_E *type) {
    // Allocate buffers with space for marshalled data
    u32 sendBytes = 4;
    u8 sendData[4];
    u32 receiveBytes = 8;
    u8 receiveData[8];
    u8 *outPtr = (u8 *)sendData;
    u32 seq_num = ++sensor->command_count;

    //write output to sendData buffer
    { //Block to allow reuse of inVal
        if(outPtr >= (sendData+sendBytes))
            return R_SDK_PKG_BUFFER_OVERFLOW;
        FLR_DVOMUX_OUTPUT_IF_E inVal = output;
        u32oBytes(inVal, (u8 *) outPtr);
        outPtr += 4;
    }

    FLR_RESULT returncode = CLIENT_dispatcher(sensor, seq_num, DVOMUX_GETTYPE, sendData, sendBytes, receiveData, &receiveBytes);
    if((u32) returncode)
        return returncode;

    u8 *inPtr = (u8 *)receiveData;

    // read source from receiveData buffer
    { //Block to allow reuse of outVal
        if(inPtr >= (receiveData+receiveBytes))
            return R_SDK_PKG_BUFFER_OVERFLOW;
        int outVal = byteToUINT32( (u8 *) inPtr);
        *source = (FLR_DVOMUX_SOURCE_E)outVal;
        inPtr+=4;
    }// end of source handling

    // read type from receiveData buffer
    { //Block to allow reuse of outVal
        if(inPtr >= (receiveData+receiveBytes))
            return R_SDK_PKG_BUFFER_OVERFLOW;
        int outVal = byteToUINT32( (u8 *) inPtr);
        *type = (FLR_DVOMUX_TYPE_E)outVal;
        inPtr+=4;
    }// end of type handling

    return R_SUCCESS;

}// End of CLIENT_pkgDvomuxGetType()
