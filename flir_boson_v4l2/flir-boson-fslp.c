// SPDX-License-Identifier: GPL-2.0
/*
 * FLIR Boson+ FSLP Communication Layer
 * Copyright (C) 2024
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/byteorder/generic.h>
#include "flir-boson.h"

#define FLIR_MAGIC_TOKEN_0    0x8E
#define FLIR_MAGIC_TOKEN_1    0xA1

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

int flir_boson_fslp_send_frame(struct flir_boson_dev *sensor,
			       const u8 *tx_data, u32 tx_len,
			       u8 *rx_data, u32 rx_len)
{
	int ret;

	/* Send frame directly via I2C */
	if (tx_len > 0) {
		ret = flir_boson_i2c_write(sensor, tx_data, tx_len);
		if (ret) {
			dev_err(sensor->dev, "Failed to send FSLP frame: %d\n", ret);
			return ret;
		}
	}

	/* Wait for processing */
	msleep(10);

	/* Read response if expected */
	if (rx_len > 0) {
		ret = flir_boson_i2c_read(sensor, rx_data, rx_len);
		if (ret) {
			dev_err(sensor->dev, "Failed to read FSLP response: %d\n", ret);
			return ret;
		}

		/* Validate response header if it looks like FSLP */
		if (rx_len >= 4 &&
		    (rx_data[0] != FLIR_MAGIC_TOKEN_0 || rx_data[1] != FLIR_MAGIC_TOKEN_1)) {
			dev_warn(sensor->dev, "Invalid FSLP response magic\n");
		}
	}

	return 0;
}

int flir_boson_set_output_interface(struct flir_boson_dev *sensor, int interface)
{
	u8 frame[] = {0x8E, 0xA1, 0x00, 0x04, 0x07, 0x00, 0x06, 0x00};
	frame[7] = interface;

	dev_dbg(sensor->dev, "Setting output interface to %d\n", interface);

	return flir_boson_fslp_send_frame(sensor, frame, sizeof(frame), NULL, 0);
}

int flir_boson_set_dvo_type(struct flir_boson_dev *sensor, u32 type)
{
	u8 frame[] = {0x8E, 0xA1, 0x00, 0x04, 0x0F, 0x00, 0x06, 0x00};
	frame[7] = type;

	dev_dbg(sensor->dev, "Setting DVO type to %d\n", type);

	return flir_boson_fslp_send_frame(sensor, frame, sizeof(frame), NULL, 0);
}

int flir_boson_set_mipi_state(struct flir_boson_dev *sensor, int state)
{
	u8 frame[] = {0x8E, 0xA1, 0x00, 0x04, 0x24, 0x00, 0x06, 0x00};
	frame[7] = state;
	int ret;

	dev_dbg(sensor->dev, "Setting MIPI state to %d\n", state);

	ret = flir_boson_fslp_send_frame(sensor, frame, sizeof(frame), NULL, 0);
	if (!ret)
		sensor->mipi_state = state;

	return ret;
}

int flir_boson_apply_settings(struct flir_boson_dev *sensor)
{
	u8 frame[] = {0x8E, 0xA1, 0x00, 0x04, 0x25, 0x00, 0x06, 0x00};

	dev_dbg(sensor->dev, "Applying custom settings\n");

	return flir_boson_fslp_send_frame(sensor, frame, sizeof(frame), NULL, 0);
}

int flir_boson_get_mipi_state(struct flir_boson_dev *sensor, int *state)
{
	u8 tx_frame[] = {0x8E, 0xA1, 0x00, 0x04, 0x26, 0x00, 0x06, 0x00};
	u8 rx_frame[8];
	int ret;

	ret = flir_boson_fslp_send_frame(sensor, tx_frame, sizeof(tx_frame),
					 rx_frame, sizeof(rx_frame));
	if (!ret && rx_frame[3] >= 4)
		*state = rx_frame[7];

	return ret;
}