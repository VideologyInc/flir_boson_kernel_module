// SPDX-License-Identifier: GPL-2.0
/*
 * FLIR Boson+ FSLP Communication Layer
 * Copyright (C) 2024
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/byteorder/generic.h>
#include "flir-boson.h"

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

int flir_boson_fslp_send_cmd(struct flir_boson_dev *sensor, u32 cmd_id,
			     const u8 *tx_data, u32 tx_len,
			     u8 *rx_data, u32 rx_len)
{
	struct flir_fslp_cmd *cmd;
	u8 *tx_buf = sensor->fslp_tx_buf;
	u8 *rx_buf = sensor->fslp_rx_buf;
	u32 total_tx_len = FLIR_FSLP_HEADER_SIZE + 4 + tx_len; /* header + cmd_id + data */
	u32 total_rx_len = FLIR_FSLP_HEADER_SIZE + rx_len;
	int ret;

	if (total_tx_len > FLIR_FSLP_MAX_DATA || total_rx_len > FLIR_FSLP_MAX_DATA)
		return -EINVAL;

	/* Build FSLP command */
	cmd = (struct flir_fslp_cmd *)tx_buf;
	cmd->magic[0] = FLIR_MAGIC_TOKEN_0;
	cmd->magic[1] = FLIR_MAGIC_TOKEN_1;
	cmd->length = cpu_to_be16(4 + tx_len); /* cmd_id + data length */

	/* Add command ID (little endian) */
	*((u32 *)cmd->data) = cpu_to_le32(cmd_id);

	/* Add command data */
	if (tx_data && tx_len > 0)
		memcpy(cmd->data + 4, tx_data, tx_len);

	/* Send command */
	ret = flir_boson_i2c_write(sensor, tx_buf, total_tx_len);
	if (ret) {
		dev_err(sensor->dev, "Failed to send FSLP command 0x%08x: %d\n",
			cmd_id, ret);
		return ret;
	}

	/* Wait for processing */
	msleep(10);

	/* Read response if expected */
	if (rx_len > 0) {
		ret = flir_boson_i2c_read(sensor, rx_buf, total_rx_len);
		if (ret) {
			dev_err(sensor->dev, "Failed to read FSLP response: %d\n", ret);
			return ret;
		}

		/* Validate response header */
		cmd = (struct flir_fslp_cmd *)rx_buf;
		if (cmd->magic[0] != FLIR_MAGIC_TOKEN_0 ||
		    cmd->magic[1] != FLIR_MAGIC_TOKEN_1) {
			dev_err(sensor->dev, "Invalid FSLP response magic\n");
			return -EPROTO;
		}

		/* Copy response data */
		if (rx_data)
			memcpy(rx_data, cmd->data, rx_len);
	}

	return 0;
}

int flir_boson_set_output_interface(struct flir_boson_dev *sensor, int interface)
{
	u32 data = cpu_to_le32(interface);

	dev_dbg(sensor->dev, "Setting output interface to %d\n", interface);

	return flir_boson_fslp_send_cmd(sensor, DVO_SET_OUTPUT_INTERFACE,
					(u8 *)&data, sizeof(data), NULL, 0);
}

int flir_boson_set_dvo_type(struct flir_boson_dev *sensor, u32 type)
{
	u32 data = cpu_to_le32(type);

	dev_dbg(sensor->dev, "Setting DVO type to %d\n", type);

	return flir_boson_fslp_send_cmd(sensor, DVO_SET_TYPE,
					(u8 *)&data, sizeof(data), NULL, 0);
}

int flir_boson_set_mipi_state(struct flir_boson_dev *sensor, int state)
{
	u32 data = cpu_to_le32(state);
	int ret;

	dev_dbg(sensor->dev, "Setting MIPI state to %d\n", state);

	ret = flir_boson_fslp_send_cmd(sensor, DVO_SET_MIPI_STATE,
				       (u8 *)&data, sizeof(data), NULL, 0);
	if (!ret)
		sensor->mipi_state = state;

	return ret;
}

int flir_boson_apply_settings(struct flir_boson_dev *sensor)
{
	dev_dbg(sensor->dev, "Applying custom settings\n");

	return flir_boson_fslp_send_cmd(sensor, DVO_APPLY_CUSTOM_SETTINGS,
					NULL, 0, NULL, 0);
}

int flir_boson_get_mipi_state(struct flir_boson_dev *sensor, int *state)
{
	u32 data;
	int ret;

	ret = flir_boson_fslp_send_cmd(sensor, DVO_GET_MIPI_STATE,
				       NULL, 0, (u8 *)&data, sizeof(data));
	if (!ret)
		*state = le32_to_cpu(data);

	return ret;
}