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

#ifndef I2C_CONNECTOR_H
#define I2C_CONNECTOR_H

#include <linux/types.h>
#include "EnumTypes.h"
#include "FunctionCodes.h"
#include "ReturnCodes.h"

/* Forward declaration to avoid circular dependency */
struct flir_boson_dev;

#define I2C_SLAVE_CP_FRAME_HEAD_SIZE        2
#define I2C_SALVE_CP_FRAME_BYTES_NUM_SIZE   2
#define I2C_SLAVE_CP_FRAME_HEADER_SIZE      (I2C_SLAVE_CP_FRAME_HEAD_SIZE + I2C_SALVE_CP_FRAME_BYTES_NUM_SIZE)

FLR_RESULT I2C_readFrame(struct flir_boson_dev *sensor, u8* readData, u32* readBytes);
FLR_RESULT I2C_writeFrame(struct flir_boson_dev *sensor, u8* writeData, u32 writeBytes);

#endif // I2C_CONNECTOR_H
