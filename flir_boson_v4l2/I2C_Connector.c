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

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/i2c.h>
#include "flir-boson.h"
#include "I2C_Connector.h"

// extern FLR_RESULT I2C_read(u8* readData, u32 readBytes);
// extern FLR_RESULT I2C_write(u8* writeData, u32 writeBytes);

static u8 frameHead[I2C_SLAVE_CP_FRAME_HEAD_SIZE] = {0x8E, 0xA1};

static void addToShiftBuffer(u8* buffer, u32 bufferSize, u8 value)
{
    for(u32 index = 0; index < (bufferSize - 1); ++index)
    {
        buffer[index] = buffer[index + 1];
    }
    buffer[bufferSize - 1] = value;
}

FLR_RESULT I2C_readFrame(struct flir_boson_dev *sensor, u8* readData, u32* readBytes)
{
    bool frameNotReady = true, headerFound = false;
    u8 retByte;
    u8* readBuffer;
    u32 bytesNumber, readSize;
    u8 headerBuffer[I2C_SLAVE_CP_FRAME_HEADER_SIZE];

    if(readData == NULL || readBytes == NULL)
        return FLR_BAD_ARG_POINTER_ERROR;

    readBuffer = &retByte;
    readSize = 1;
    do
    {
       	// dev_dbg(sensor->dev, "I2C_readFrame: reading %d bytes\n", readSize);
        struct i2c_msg msg = { .addr=sensor->i2c_client->addr, .flags=I2C_M_RD, .len=readSize, .buf=readBuffer	};
        if(i2c_transfer(sensor->i2c_client->adapter, &msg, 1) != 1)
            return FLR_COMM_ERROR_READING_COMM;

        if(headerFound)
        {
            *readBytes = readSize;
            frameNotReady = false;
        }
        else
        {
            addToShiftBuffer(headerBuffer, I2C_SLAVE_CP_FRAME_HEADER_SIZE, retByte);
            if(memcmp(headerBuffer, frameHead, I2C_SLAVE_CP_FRAME_HEAD_SIZE) == 0)
            {
                bytesNumber = ((u32)headerBuffer[I2C_SLAVE_CP_FRAME_HEAD_SIZE]) << 8 | (u32)headerBuffer[I2C_SLAVE_CP_FRAME_HEAD_SIZE + 1];
                readSize = bytesNumber;
                readBuffer = readData;
                headerFound = true;
            }
        }
    } while(frameNotReady);

    return FLR_OK;
}

FLR_RESULT I2C_writeFrame(struct flir_boson_dev *sensor, u8* writeData, u32 writeBytes)
{
    if(writeData == NULL)
        return FLR_BAD_ARG_POINTER_ERROR;

    if(writeBytes < 1)
        return FLR_ERROR;

    u8 sendFrame[FLIR_FSLP_MAX_DATA];
    u8* ptr = sendFrame;

    memcpy(ptr, frameHead, I2C_SLAVE_CP_FRAME_HEAD_SIZE);
    ptr += I2C_SLAVE_CP_FRAME_HEAD_SIZE;
    *ptr = (u8)((writeBytes >> 8) & 0xFF);
    ptr++;
    *ptr = (u8)(writeBytes & 0xFF);
    ptr++;
    memcpy(ptr, writeData, writeBytes);

    ptr = sendFrame;
    u32 total_len = writeBytes + I2C_SLAVE_CP_FRAME_HEADER_SIZE;
    struct i2c_msg msg = { .addr=sensor->i2c_client->addr, .flags = 0, .len=total_len, .buf=ptr	};
   	// dev_dbg(sensor->dev, "I2C_writeFrame: writing %d bytes\n", total_len);

	int res = i2c_transfer(sensor->i2c_client->adapter, &msg, 1);
	if (res != 1)
	    return FLR_COMM_ERROR_WRITING_COMM;

    return FLR_OK;
}
