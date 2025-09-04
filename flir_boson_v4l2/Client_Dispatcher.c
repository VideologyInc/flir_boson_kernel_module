//  /////////////////////////////////////////////////////
//  // DO NOT EDIT.  This is a machine generated file. //
//  /////////////////////////////////////////////////////
/******************************************************************************/
/*                                                                            */
/*  Copyright (C) 2018, FLIR Systems                                          */
/*  All rights reserved.                                                      */
/*                                                                            */
/*  This document is controlled to FLIR Technology Level 2. The information   */
/*  contained in this document pertains to a dual use product controlled for  */
/*  export by the Export Administration Regulations (EAR). Diversion contrary */
/*  to US law is prohibited. US Department of Commerce authorization is not   */
/*  required prior to export or transfer to foreign persons or parties unless */
/*  otherwise prohibited.                                                     */
/*                                                                            */
/******************************************************************************/

#include <linux/delay.h>

#include "flir-boson.h"
#include "Client_Dispatcher.h"

void byteToUINT_32(const u8 *inBuff, u32 *outVal) {
	const u8 *inPtr = inBuff;
	*outVal = ((u32)inPtr[0] << 24) | ((u32)inPtr[1] << 16) | ((u32)inPtr[2] << 8) | (u32)inPtr[3];
}

void UINT_32ToByte(const u32 inVal, const u8 *outBuff){
	u8 *outPtr = (u8 *)outBuff;
	*outPtr++ = (u8) (inVal>>24 & 0xff);
	*outPtr++ = (u8) (inVal>>16 & 0xff);
	*outPtr++ = (u8) (inVal>>8 & 0xff);
	*outPtr = (u8) (inVal & 0xff);
}

// Asynchronous (MultiService compatible) transmit part
FLR_RESULT CLIENT_dispatcher_Tx(struct flir_boson_dev *sensor, u32 seqNum, FLR_FUNCTION fnID, const u8 *sendData, const u32 sendBytes) {

    u32 i;

    // Allocated buffer with extra space for payload header
    u8 sendPayload[530];
    u8 *pyldPtr = (u8 *)sendPayload;

    // Write sequence number to first 4 bytes
    UINT_32ToByte(seqNum, (const u8 *)pyldPtr);
    pyldPtr += 4;

    // Write function ID to second 4 bytes
    UINT_32ToByte((const u32) fnID, (const u8 *)pyldPtr);
    pyldPtr += 4;

    // Write 0xFFFFFFFF to third 4 bytes
    UINT_32ToByte(0xFFFFFFFF, (const u8 *)pyldPtr);
    pyldPtr += 4;

    // Copy sendData to payload buffer
    if (sendBytes) {
        u8 *dataPtr = (u8 *)sendData;
        for(i = 0; i<sendBytes; i++) {
            *pyldPtr++ = *dataPtr++;
        }
    }
    if(I2C_writeFrame(sensor, sendPayload, sendBytes + 12) != FLR_OK)
        return FLR_COMM_ERROR_WRITING_COMM;

    return R_SUCCESS;
}
// Asynchronous (MultiService compatible) receive part
FLR_RESULT CLIENT_dispatcher_Rx(struct flir_boson_dev *sensor, u32 *seqNum, u32 *fnID, const u8 *receiveData, u32 *receiveBytes) {

    u32 i;

    // Allocated buffer with extra space for return data
    u8 receivePayload[530];
    u8 *inPtr = (u8 *)receivePayload;

    *receiveBytes+=12;
    if(I2C_readFrame(sensor, receivePayload, receiveBytes) != FLR_OK){
        return FLR_COMM_ERROR_READING_COMM;
    }

    if (*receiveBytes < 12) {
        if(I2C_readFrame(sensor, receivePayload, receiveBytes) != FLR_OK)
            return FLR_COMM_ERROR_READING_COMM;
    }

    if (*receiveBytes < 12)
        return FLR_COMM_ERROR_READING_COMM;

    // Evaluate sequence bytes as UINT_32
    u32 returnSequence;
    byteToUINT_32( (const u8 *) inPtr, &returnSequence);
    inPtr += 4;

    // Ensure that received sequence matches sent sequence
    if(seqNum){
        *seqNum = returnSequence;
    }

    // Evaluate CMD ID bytes as UINT_32
    u32 cmdID;
    byteToUINT_32( (const u8 *) inPtr, &cmdID);
    inPtr += 4;

    // Ensure that received CMD ID matches sent CMD ID
    if(fnID){
        *fnID = cmdID;
    }

    // Evaluate Payload Status bytes as UINT_32
    u32 pyldStatus;
    byteToUINT_32( (const u8 *) inPtr, &pyldStatus);
    inPtr += 4;

    const FLR_RESULT returncode = (FLR_RESULT) pyldStatus;
    // Check for any errorcode
    if(returncode != R_SUCCESS){
        return returncode;
    }

    // Now have Good Tx, Good Sequence, Good CMD ID, and Good Status.
    // inPtr at Data block, fill receiveData buffer with outPtr
    u8 *outPtr = (u8 *)receiveData;
    // decrement receiveBytes by 12 (len of header bytes)
    *receiveBytes-=12;

    u32 localvar = *receiveBytes; //shouldn't have to do this, but it works.
    for(i=0;i<localvar;i++) {
        *outPtr++ = *inPtr++;
    }

    return R_SUCCESS;
} // End CLIENT_dispatcher()

// Synchronous (potentially MultiService incompatible) transmit+receive variant
FLR_RESULT CLIENT_dispatcher(struct flir_boson_dev *sensor, u32 seqNum, FLR_FUNCTION fnID, const u8 *sendData, const u32 sendBytes, const u8 *receiveData, u32 *receiveBytes)
{
    u32 returnSequence;
    u32 cmdID;

    // dev_dbg(sensor->dev, "%s: seqNum=%u, fnID=%u, sendBytes=%u, receiveBytes=%u\n", __func__, seqNum, (u32)fnID, sendBytes, *receiveBytes);
    FLR_RESULT res = CLIENT_dispatcher_Tx(sensor, seqNum, fnID, sendData, sendBytes);
    if (res){
        dev_err(sensor->dev, "%s: CLIENT_dispatcher_Tx failed: %s\n", __func__, flr_result_to_string(res));
        return res;
    }
    // msleep(10);
    res = CLIENT_dispatcher_Rx(sensor, &returnSequence, &cmdID, receiveData, receiveBytes);
    if (res){
        // dev_err(sensor->dev, "%s: CLIENT_dispatcher_Rx failed: %s, returnSequence=%08x, cmdID=%08x\n", __func__, flr_result_to_string(res), returnSequence, cmdID);
        return res;
    }
    // else {
        // dev_dbg(sensor->dev, "%s: returnSequence=%08x, cmdID=%08x, receiveBytes=%u\n", __func__, returnSequence, cmdID, *receiveBytes);
    // }
    if (returnSequence ^ seqNum)
        return R_SDK_DSPCH_SEQUENCE_MISMATCH;

    if (cmdID ^ (u32) fnID)
        return R_SDK_DSPCH_ID_MISMATCH;

    return R_SUCCESS;
}
