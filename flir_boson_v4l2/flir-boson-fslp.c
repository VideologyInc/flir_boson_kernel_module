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
#include <linux/delay.h>
#include <linux/byteorder/generic.h>
#include "flir-boson.h"

/* ========================================================================
 * Layer 0: Raw I2C Transport (with Simulation Mode Support)
 * ======================================================================== */

#ifdef FLIR_SIMULATION_MODE

/* Simulation Mode: Mock I2C with printk logging */
static int flir_simulate_i2c_write(struct flir_boson_dev *sensor,
				   const u8 *data, size_t len)
{
	int i;

	dev_info(sensor->dev, "FSLP_SIM_TX: %zu bytes:", len);

	/* Log frame in hex format for validation */
	for (i = 0; i < len; i++) {
		if (i % 16 == 0)
			printk(KERN_CONT "\nFSLP_SIM_TX: %04X: ", i);
		printk(KERN_CONT "%02X ", data[i]);
	}
	printk(KERN_CONT "\n");

	/* Validate FSLP frame structure */
	if (len >= 4 && data[0] == FLIR_MAGIC_TOKEN_0 && data[1] == FLIR_MAGIC_TOKEN_1) {
		u16 payload_len = (data[2] << 8) | data[3];
		dev_info(sensor->dev, "FSLP_SIM: Valid frame, payload_len=%u", payload_len);

		if (len == 4 + payload_len) {
			dev_info(sensor->dev, "FSLP_SIM: Frame length correct");
		} else {
			dev_warn(sensor->dev, "FSLP_SIM: Frame length mismatch: got %zu, expected %u",
				 len, 4 + payload_len);
		}
	} else {
		dev_warn(sensor->dev, "FSLP_SIM: Invalid FSLP frame");
	}

	return 0; /* Always succeed in simulation */
}

static int flir_simulate_i2c_read(struct flir_boson_dev *sensor,
				  u8 *data, size_t len)
{
	/* Generate mock camera response */
	if (len >= 16) {
		/* Simulate realistic camera response */
		u8 *ptr = data;

		/* I2C FSLP header */
		*ptr++ = FLIR_MAGIC_TOKEN_0;  /* 0x8E */
		*ptr++ = FLIR_MAGIC_TOKEN_1;  /* 0xA1 */
		*ptr++ = 0x00;                /* Length high byte */
		*ptr++ = len - 4;             /* Length low byte */

		/* Response payload header (echo sequence + command, success status) */
		*ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x01; /* Sequence = 1 */
		*ptr++ = 0x00; *ptr++ = 0x06; *ptr++ = 0x00; *ptr++ = 0x24; /* DVO_SET_MIPI_STATE */
		*ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; /* Status = success */

		/* Response data (for GET commands) */
		if (len > 16) {
			*ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x02; /* MIPI_STATE_ACTIVE */
		}
	}

	dev_info(sensor->dev, "FSLP_SIM_RX: Generated %zu bytes response", len);
	return 0;
}

#define flir_boson_i2c_write(sensor, data, len) flir_simulate_i2c_write(sensor, data, len)
#define flir_boson_i2c_read(sensor, data, len) flir_simulate_i2c_read(sensor, data, len)

#else

/* Real Hardware Mode: Actual I2C Communication */
static int flir_boson_i2c_write(struct flir_boson_dev *sensor,
				const u8 *data, size_t len)
{
	struct i2c_msg msg = {
		.addr = sensor->i2c_client->addr,
		.flags = 0,
		.len = len,
		.buf = (u8 *)data,
	};

	return i2c_transfer(sensor->i2c_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
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

	return i2c_transfer(sensor->i2c_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
}

#endif /* FLIR_SIMULATION_MODE */

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
	ret = flir_boson_i2c_read(sensor, header, 4);
	if (ret) {
		dev_err(sensor->dev, "Failed to read FSLP header: %d\n", ret);
		return ret;
	}

	/* Validate magic tokens - matches I2CFslp.py:50-51 */
	if (header[0] != FLIR_MAGIC_TOKEN_0 || header[1] != FLIR_MAGIC_TOKEN_1) {
		dev_err(sensor->dev, "Invalid FSLP magic: 0x%02X 0x%02X\n",
			header[0], header[1]);
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
		ret = flir_boson_i2c_read(sensor, payload, payload_len);
		if (ret) {
			dev_err(sensor->dev, "Failed to read FSLP payload: %d\n", ret);
			return ret;
		}
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
int flir_command_dispatcher(struct flir_boson_dev *sensor, u32 seq_num, u32 fn_id,
			    const u8 *send_data, u32 send_bytes,
			    u8 *receive_data, u32 *receive_bytes)
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
		return -EINVAL;
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
	ret = flir_fslp_send_frame(sensor, 0x00, command_payload, 12 + send_bytes);
	if (ret) {
		dev_err(sensor->dev, "Failed to send command 0x%08X: %d\n", fn_id, ret);
		return ret;
	}

	/* Wait for processing */
	msleep(10);

	/* Read response if expected - matches Client_Dispatcher.py:40-48 */
	if (receive_bytes && *receive_bytes > 0) {
		expected_resp_len = *receive_bytes + 12; /* Add header length */

		/* Retry logic for sequence mismatch - matches Client_Dispatcher.py:40-65 */
		for (retry = 0; retry < 2; retry++) {
			ret = flir_fslp_read_frame(sensor, 0x00, response_payload, expected_resp_len);
			if (ret) {
				dev_err(sensor->dev, "Failed to read response: %d\n", ret);
				return ret;
			}

			/* Validate response length */
			if (expected_resp_len < 12) {
				if (retry == 0) {
					dev_warn(sensor->dev, "Short response, retrying...\n");
					continue;
				}
				dev_err(sensor->dev, "Response too short: %u bytes\n", expected_resp_len);
				return -EPROTO;
			}

			resp_ptr = response_payload;

			/* Validate sequence number - matches Client_Dispatcher.py:52-64 */
			return_seq = byteToUINT32(resp_ptr);
			resp_ptr += 4;

			if (return_seq != seq_num) {
				dev_warn(sensor->dev, "Sequence mismatch: exp 0x%08X, got 0x%08X\n",
					 seq_num, return_seq);
				if (retry == 0) {
					dev_warn(sensor->dev, "Retrying read...\n");
					continue;
				}
				return -EPROTO;
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
			return -EPROTO;
		}

		/* Validate status - matches Client_Dispatcher.py:75-87 */
		status = byteToUINT32(resp_ptr);
		resp_ptr += 4;

		if (status != 0) {
			dev_err(sensor->dev, "Command 0x%08X failed with status 0x%08X\n",
				fn_id, status);
			return -EREMOTEIO;
		}

		/* Copy response data - matches Client_Dispatcher.py:90-92 */
		if (receive_data && *receive_bytes > 0) {
			memcpy(receive_data, resp_ptr, *receive_bytes);
		}

		dev_dbg(sensor->dev, "Command 0x%08X completed successfully\n", fn_id);
	}

	return 0;
}

/* ========================================================================
 * Layer 3: Command Packagers (matches Client_Packager.py/c patterns)
 * ======================================================================== */

int flir_boson_set_output_interface(struct flir_boson_dev *sensor, int interface)
{
	u8 send_data[4];
	u32 seq_num = ++sensor->command_count;

	dev_dbg(sensor->dev, "Setting output interface to %d\n", interface);

	UINT32_ToBytes(interface, send_data);
	return flir_command_dispatcher(sensor, seq_num, DVO_SET_OUTPUT_INTERFACE,
				       send_data, sizeof(send_data), NULL, NULL);
}

int flir_boson_set_dvo_type(struct flir_boson_dev *sensor, u32 type)
{
	u8 send_data[4];
	u32 seq_num = ++sensor->command_count;

	dev_dbg(sensor->dev, "Setting DVO type to %u\n", type);

	UINT32_ToBytes(type, send_data);
	return flir_command_dispatcher(sensor, seq_num, DVO_SET_TYPE,
				       send_data, sizeof(send_data), NULL, NULL);
}

int flir_boson_set_mipi_state(struct flir_boson_dev *sensor, int state)
{
	u8 send_data[4];
	u32 seq_num = ++sensor->command_count;
	int ret;

	dev_dbg(sensor->dev, "Setting MIPI state to %d\n", state);

	UINT32_ToBytes(state, send_data);
	ret = flir_command_dispatcher(sensor, seq_num, DVO_SET_MIPI_STATE,
				      send_data, sizeof(send_data), NULL, NULL);
	if (!ret)
		sensor->mipi_state = state;

	return ret;
}

int flir_boson_apply_settings(struct flir_boson_dev *sensor)
{
	u32 seq_num = ++sensor->command_count;

	dev_dbg(sensor->dev, "Applying custom settings\n");

	return flir_command_dispatcher(sensor, seq_num, DVO_APPLY_CUSTOM_SETTINGS,
				       NULL, 0, NULL, NULL);
}

int flir_boson_get_mipi_state(struct flir_boson_dev *sensor, int *state)
{
	u8 receive_data[4];
	u32 receive_bytes = sizeof(receive_data);
	u32 seq_num = ++sensor->command_count;
	int ret;

	ret = flir_command_dispatcher(sensor, seq_num, DVO_GET_MIPI_STATE,
				      NULL, 0, receive_data, &receive_bytes);
	if (!ret && receive_bytes >= 4) {
		*state = byteToUINT32(receive_data);
		dev_dbg(sensor->dev, "Current MIPI state: %d\n", *state);
	}

	return ret;
}

/* ========================================================================
 * Legacy compatibility function (for existing IOCTL interface)
 * ======================================================================== */

int flir_boson_fslp_send_frame(struct flir_boson_dev *sensor,
			       const u8 *tx_data, u32 tx_len,
			       u8 *rx_data, u32 rx_len)
{
	int ret;

	/* Send frame directly via I2C (legacy mode) */
	if (tx_len > 0) {
		ret = flir_boson_i2c_write(sensor, tx_data, tx_len);
		if (ret) {
			dev_err(sensor->dev, "Failed to send legacy FSLP frame: %d\n", ret);
			return ret;
		}
	}

	/* Wait for processing */
	msleep(10);

	/* Read response if expected */
	if (rx_len > 0) {
		ret = flir_boson_i2c_read(sensor, rx_data, rx_len);
		if (ret) {
			dev_err(sensor->dev, "Failed to read legacy FSLP response: %d\n", ret);
			return ret;
		}

		/* Basic validation */
		if (rx_len >= 4 &&
		    (rx_data[0] != FLIR_MAGIC_TOKEN_0 || rx_data[1] != FLIR_MAGIC_TOKEN_1)) {
			dev_warn(sensor->dev, "Invalid legacy FSLP response magic\n");
		}
	}

	return 0;
}