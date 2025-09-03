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

/* ========================================================================
 * Layer 0: Raw I2C Transport
 * ======================================================================== */

/* Hardware Mode: Real I2C Communication */
static int flir_boson_i2c_write(struct flir_boson_dev *sensor,
				const u8 *data, size_t len)
{
	struct i2c_msg msg = {
		.addr = sensor->i2c_client->addr,
		.flags = 0,
		.len = len,
		.buf = (u8 *)data,
	};
	int ret = 0;

	// dev_dbg(sensor->dev, "I2C_WRITE: addr=0x%02x, len=%zu",
		// sensor->i2c_client->addr, len);
	// print_hex_dump_debug("I2C_WRITE_DATA: ", DUMP_PREFIX_OFFSET, 16, 1, data, len, true);

	ret = i2c_transfer(sensor->i2c_client->adapter, &msg, 1);

	// dev_dbg(sensor->dev, "I2C_WRITE: result=%d (expected=1)", ret);

	return ret == 1 ? 0 : -EIO;
}

static int flir_boson_i2c_read(struct flir_boson_dev *sensor,
			       u8 *data, size_t len)
{
	struct i2c_msg msg = {
		.addr = sensor->i2c_client->addr,
		.flags = I2C_M_RD,
		.len = len,
		.buf = data,
	};
	int ret = 0;

	// dev_dbg(sensor->dev, "I2C_READ: addr=0x%02x, len=%zu", sensor->i2c_client->addr, len);

	ret = i2c_transfer(sensor->i2c_client->adapter, &msg, 1);

	// dev_dbg(sensor->dev, "I2C_READ: result=%d (expected=1)", ret);
	if (ret == 1) {
		// print_hex_dump_debug("I2C_READ_DATA: ", DUMP_PREFIX_OFFSET, 16, 1, data, len, true);
	}

	return ret == 1 ? 0 : -EIO;
}

/* UINT32_ToBytes - copied from SDK/ClientFiles_MSVC/Serializer_BuiltIn.c */
static void UINT32_ToBytes(u32 inVal, u8 *outBuff)
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
 * Layer 1: I2C FSLP Framing Layer (matches I2CFslp.py exactly)
 * ======================================================================== */

/**
 * flir_fslp_send_frame - Send FSLP frame over I2C (matches I2CFslp.sendFrame)
 * @sensor: FLIR sensor device
 * @channel_id: Channel ID (0x00 for commands)
 * @payload: Command payload data (from dispatcher layer)
 * @payload_len: Length of payload data
 *
 * Implements exact I2CFslp.sendFrame() protocol:
 * - Magic tokens: [0x8E, 0xA1]
 * - Big-endian u16 length (payload only, not including I2C header)
 * - Payload data (12-byte command header + command data)
 */
int flir_fslp_send_frame(struct flir_boson_dev *sensor, u8 channel_id,
			 const u8 *payload, u32 payload_len)
{
	u8 frame_buffer[FLIR_FSLP_MAX_DATA + 4];
	u8 *ptr = frame_buffer;

	if (payload_len > FLIR_FSLP_MAX_DATA) {
		dev_err(sensor->dev, "Payload too large: %u bytes\n", payload_len);
		return -EINVAL;
	}

	/* I2C FSLP Frame Header (4 bytes) - matches I2CFslp.py:36-38 */
	*ptr++ = FLIR_MAGIC_TOKEN_0;        /* 0x8E */
	*ptr++ = FLIR_MAGIC_TOKEN_1;        /* 0xA1 */
	*ptr++ = (u8)(payload_len >> 8);    /* Length high byte (payload only!) */
	*ptr++ = (u8)(payload_len & 0xff);  /* Length low byte */

	/* Copy payload (command dispatcher data) */
	memcpy(ptr, payload, payload_len);

	dev_dbg(sensor->dev, "FSLP send: channel=0x%02X, payload_len=%u\n",
		channel_id, payload_len);
	// print_hex_dump_debug("FSLP_FRAME: ", DUMP_PREFIX_OFFSET, 16, 1, frame_buffer, 4 + payload_len, true);

	return flir_boson_i2c_write(sensor, frame_buffer, 4 + payload_len);
}

/**
 * flir_fslp_read_frame - Read FSLP frame from I2C (matches I2CFslp.readFrame)
 * @sensor: FLIR sensor device
 * @channel_id: Channel ID (0x00 for commands)
 * @payload: Buffer for payload data
 * @expected_len: Expected payload length
 *
 * Implements exact I2CFslp.readFrame() protocol:
 * - Read 4-byte I2C header (magic + length)
 * - Validate magic tokens
 * - Read payload data based on length
 */
int flir_fslp_read_frame(struct flir_boson_dev *sensor, u8 channel_id,
			 u8 *payload, u32 expected_len)
{
	u8 header[4];
	u16 payload_len;
	int ret;

	/* Read I2C FSLP header (4 bytes) */
	dev_dbg(sensor->dev, "FSLP read: reading header (4 bytes)");
	ret = flir_boson_i2c_read(sensor, header, 4);
	if (ret) {
		dev_err(sensor->dev, "Failed to read FSLP header: %d\n", ret);
		return ret;
	}
	// print_hex_dump_debug("FSLP_HEADER: ", DUMP_PREFIX_OFFSET, 16, 1, header, 4, true);

	/* Validate magic tokens - matches I2CFslp.py:50-51 */
	if (header[0] != FLIR_MAGIC_TOKEN_0 || header[1] != FLIR_MAGIC_TOKEN_1) {
		dev_err(sensor->dev, "Invalid FSLP magic: 0x%02X 0x%02X\n",	header[0], header[1]);
		return -EPROTO;
	}

	/* Extract payload length (big-endian) - matches I2CFslp.py:53 */
	payload_len = (header[2] << 8) | header[3];

	/* Validate payload length - matches I2CFslp.py:54-55 */
	if (payload_len != expected_len) {
		dev_warn(sensor->dev, "Length mismatch: declared %u, expected %u\n",
			 payload_len, expected_len);
	}

	/* Read payload data */
	if (payload_len > 0) {
		dev_dbg(sensor->dev, "FSLP read: reading payload (%u bytes)", payload_len);
		ret = flir_boson_i2c_read(sensor, payload, payload_len);
		if (ret) {
			dev_err(sensor->dev, "Failed to read FSLP payload: %d\n", ret);
			return ret;
		}
		print_hex_dump_debug("FSLP_PAYLOAD: ", DUMP_PREFIX_OFFSET, 16, 1, payload, payload_len, true);
	}

	dev_dbg(sensor->dev, "FSLP recv: channel=0x%02X, payload_len=%u\n",
		channel_id, payload_len);

	return 0;
}

/* ========================================================================
 * Layer 2: Command Dispatcher (matches Client_Dispatcher.py/c exactly)
 * ======================================================================== */

/**
 * flir_command_dispatcher - Dispatch FLIR SDK command (matches CLIENT_dispatch)
 * @sensor: FLIR sensor device
 * @seq_num: Sequence number for command tracking
 * @fn_id: Function ID (SDK command code)
 * @send_data: Command-specific data to send
 * @send_bytes: Length of send_data
 * @receive_data: Buffer for response data (without headers)
 * @receive_bytes: Expected response data length (updated with actual)
 *
 * Implements exact CLIENT_dispatch() protocol from Client_Dispatcher.py:11-93
 */
FLR_RESULT flir_command_dispatcher(struct flir_boson_dev *sensor, u32 seq_num, u32 fn_id, const u8 *send_data, u32 send_bytes, u8 *receive_data, u32 *receive_bytes)
{
	u8 command_payload[FLIR_FSLP_MAX_DATA];
	u8 response_payload[FLIR_FSLP_MAX_DATA];
	u8 *ptr = command_payload;
	const u8 *resp_ptr;
	u32 return_seq, cmd_id, status;
	u32 expected_resp_len = 0;
	int ret, retry;

	if (send_bytes > (FLIR_FSLP_MAX_DATA - 12)) {
		dev_err(sensor->dev, "Command data too large: %u bytes\n", send_bytes);
		return R_SDK_PKG_BUFFER_OVERFLOW;
	}

	/* Build 12-byte command header - matches Client_Dispatcher.py:16-26 */
	UINT32_ToBytes(seq_num, ptr); ptr += 4;      /* Sequence number */
	UINT32_ToBytes(fn_id, ptr); ptr += 4;        /* Function ID */
	UINT32_ToBytes(0xFFFFFFFF, ptr); ptr += 4;   /* Status placeholder */

	/* Copy command-specific data - matches Client_Dispatcher.py:28-31 */
	if (send_data && send_bytes > 0) {
		memcpy(ptr, send_data, send_bytes);
	}

	/* Send command via I2C FSLP framing layer - matches Client_Dispatcher.py:38 */
	dev_dbg(sensor->dev, "Dispatching command: seq=0x%08X, fn_id=0x%08X, send_bytes=%u", seq_num, fn_id, send_bytes);
	// print_hex_dump_debug("CMD_PAYLOAD: ", DUMP_PREFIX_OFFSET, 16, 1, command_payload, 12 + send_bytes, true);

	ret = flir_fslp_send_frame(sensor, 0x00, command_payload, 12 + send_bytes);
	if (ret) {
		dev_err(sensor->dev, "Failed to send command 0x%08X: %d\n", fn_id, ret);
		return FLR_COMM_ERROR_WRITING_COMM;
	}

	/* Read response if expected - matches Client_Dispatcher.py:40-48 */
	if (receive_bytes && *receive_bytes > 0) {
		expected_resp_len = *receive_bytes + 12; /* Add header length */

		/* Retry logic for sequence mismatch - matches Client_Dispatcher.py:40-65 */
		for (retry = 0; retry < 2; retry++) {
			dev_dbg(sensor->dev, "Reading response: expected_len=%u, retry=%d", expected_resp_len, retry);
			ret = flir_fslp_read_frame(sensor, 0x00, response_payload, expected_resp_len);
			if (ret) {
				dev_err(sensor->dev, "Failed to read response: %d\n", ret);
				return FLR_COMM_ERROR_READING_COMM;
			}
			// print_hex_dump_debug("RESP_PAYLOAD: ", DUMP_PREFIX_OFFSET, 16, 1, response_payload, expected_resp_len, true);

			/* Validate response length */
			if (expected_resp_len < 12) {
				if (retry == 0) {
					dev_warn(sensor->dev, "Short response, retrying...\n");
					continue;
				}
				dev_err(sensor->dev, "Response too short: %u bytes\n", expected_resp_len);
				return FLR_COMM_ERROR_READING_COMM;
			}

			resp_ptr = response_payload;

			/* Validate sequence number - matches Client_Dispatcher.py:52-64 */
			return_seq = byteToUINT32(resp_ptr);
			resp_ptr += 4;

			if (return_seq != seq_num) {
				dev_warn(sensor->dev, "Sequence mismatch: exp 0x%08X, got 0x%08X\n", seq_num, return_seq);
				if (retry == 0) {
					dev_warn(sensor->dev, "Retrying read...\n");
					continue;
				}
				return R_SDK_DSPCH_SEQUENCE_MISMATCH;
			} else {
				break; /* Sequence OK */
			}
		}

		/* Validate command ID - matches Client_Dispatcher.py:68-73 */
		cmd_id = byteToUINT32(resp_ptr);
		resp_ptr += 4;

		if (cmd_id != fn_id) {
			dev_err(sensor->dev, "Command ID mismatch: exp 0x%08X, got 0x%08X\n",
				fn_id, cmd_id);
			return R_SDK_DSPCH_ID_MISMATCH;
		}

		/* Validate status - matches Client_Dispatcher.py:75-87 */
		status = byteToUINT32(resp_ptr);
		resp_ptr += 4;

		if (status != R_SUCCESS) {
			dev_err(sensor->dev, "Command 0x%08X failed with status 0x%08X (%s)\n",
				fn_id, status, flr_result_to_string((FLR_RESULT)status));
			return (FLR_RESULT)status;
		}

		/* Copy response data - matches Client_Dispatcher.py:90-92 */
		if (receive_data && *receive_bytes > 0) {
			memcpy(receive_data, resp_ptr, *receive_bytes);
		}

		dev_dbg(sensor->dev, "Command 0x%08X completed successfully\n", fn_id);
	}

	return R_SUCCESS;
}

/* ========================================================================
 * Layer 3: Command Packagers (matches Client_Packager.py/c patterns)
 * ======================================================================== */

FLR_RESULT flir_boson_send_int_cmd(struct flir_boson_dev *sensor, u32 cmd, u32 val)
{
	u8 send_data[4];
	u32 seq_num = ++sensor->command_count;
	FLR_RESULT ret;

	// dev_dbg(sensor->dev, "CMD: flir_boson_set_output_interface(interface=%d)", interface);
	// dev_dbg(sensor->dev, "CMD: seq_num=0x%08X, fn_id=0x%08X", seq_num, DVO_SET_OUTPUT_INTERFACE);
	UINT32_ToBytes(val, send_data);
	// print_hex_dump_debug("CMD_DATA: ", DUMP_PREFIX_OFFSET, 16, 1, send_data, sizeof(send_data), true);

	ret = flir_command_dispatcher(sensor, seq_num, cmd, send_data, sizeof(send_data), NULL, NULL);

	dev_dbg(sensor->dev, "CMD: flir_boson_set_output_interface result=%d", ret);
	return ret;
}

FLR_RESULT flir_boson_get_int_val(struct flir_boson_dev *sensor, u32 cmd, u32 *val)
{
	u8 receive_data[4];
	u32 receive_bytes = sizeof(receive_data);
	u32 seq_num = ++sensor->command_count;
	FLR_RESULT ret;

	ret = flir_command_dispatcher(sensor, seq_num, cmd, NULL, 0, receive_data, &receive_bytes);
	if (ret == R_SUCCESS && receive_bytes >= 4) {
		*val = byteToUINT32(receive_data);
		// dev_dbg(sensor->dev, "CMD: Got MIPI state: %d", *state);
		// print_hex_dump_debug("CMD_RESP: ", DUMP_PREFIX_OFFSET, 16, 1, receive_data, receive_bytes, true);
	}

	return ret;
}

/* ========================================================================
 * Legacy compatibility function (for existing IOCTL interface)
 * ======================================================================== */

// int flir_boson_fslp_send_frame(struct flir_boson_dev *sensor,
// 			       const u8 *tx_data, u32 tx_len,
// 			       u8 *rx_data, u32 rx_len)
// {
// 	int ret;

// 	dev_dbg(sensor->dev, "LEGACY: flir_boson_fslp_send_frame(tx_len=%u, rx_len=%u)", tx_len, rx_len);

// 	/* Send frame directly via I2C (legacy mode) */
// 	if (tx_len > 0) {
// 		// print_hex_dump_debug("LEGACY_TX: ", DUMP_PREFIX_OFFSET, 16, 1, tx_data, tx_len, true);
// 		ret = flir_boson_i2c_write(sensor, tx_data, tx_len);
// 		if (ret) {
// 			dev_err(sensor->dev, "Failed to send legacy FSLP frame: %d\n", ret);
// 			return ret;
// 		}
// 	}

// 	/* Wait for processing */
// 	msleep(10);

// 	/* Read response if expected */
// 	if (rx_len > 0) {
// 		ret = flir_boson_i2c_read(sensor, rx_data, rx_len);
// 		if (ret) {
// 			dev_err(sensor->dev, "Failed to read legacy FSLP response: %d\n", ret);
// 			return ret;
// 		}

// 		// print_hex_dump_debug("LEGACY_RX: ", DUMP_PREFIX_OFFSET, 16, 1, rx_data, rx_len, true);

// 		/* Basic validation */
// 		if (rx_len >= 4 &&
// 		    (rx_data[0] != FLIR_MAGIC_TOKEN_0 || rx_data[1] != FLIR_MAGIC_TOKEN_1)) {
// 			dev_warn(sensor->dev, "Invalid legacy FSLP response magic\n");
// 		}
// 	}

// 	dev_dbg(sensor->dev, "LEGACY: flir_boson_fslp_send_frame completed successfully");
// 	return 0;
// }
