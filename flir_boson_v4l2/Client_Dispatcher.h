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

#ifndef CLIENT_DISPATCHER_H
#define CLIENT_DISPATCHER_H

#include <linux/types.h>
#include "EnumTypes.h"
#include "FunctionCodes.h"
#include "ReturnCodes.h"

/* Forward declaration to avoid circular dependency */
struct flir_boson_dev;

FLR_RESULT CLIENT_dispatcher_Tx(struct flir_boson_dev *sensor, u32 seqNum, FLR_FUNCTION fnID, const u8 *sendData, const u32 sendBytes);
FLR_RESULT CLIENT_dispatcher_Rx(struct flir_boson_dev *sensor, u32 *seqNum, u32 *fnID, const u8 *receiveData, u32 *receiveBytes);
FLR_RESULT CLIENT_dispatcher(struct flir_boson_dev *sensor, u32 seqNum, FLR_FUNCTION fnID, const u8 *sendData, const u32 sendBytes, const u8 *receiveData, u32 *receiveBytes);

#endif
