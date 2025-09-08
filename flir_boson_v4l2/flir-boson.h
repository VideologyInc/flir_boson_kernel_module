/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FLIR Boson+ MIPI Camera V4L2 Driver
 * Copyright (C) 2024
 */

#ifndef FLIR_BOSON_H
#define FLIR_BOSON_H

#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "EnumTypes.h"
#include "FunctionCodes.h"
#include "ReturnCodes.h"
/* Forward declarations to avoid circular includes */

#define FLIR_BOSON_NAME "flir-boson"
#define FLIR_BOSON_I2C_ADDR 0x6A

/* FSLP Protocol Constants */
#define FLIR_MAGIC_TOKEN_0    0x8E
#define FLIR_MAGIC_TOKEN_1    0xA1
#define FLIR_FSLP_HEADER_SIZE 4
#define FLIR_FSLP_MAX_DATA    256

/* FLIR SDK Command Codes */

/* Boson Module Commands */
#define BOSON_GETCAMERASN          0x00050002

/* Supported Formats */
struct flir_boson_format {
	u32 code;
	u32 flir_type;
	u32 flir_mux_type;
	u8 bpp;
	const char *name;
};

/* Supported Frame Sizes */
struct flir_boson_framesize {
	u32 width;
	u32 height;
	u32 max_fps;
};

/* FSLP Command Structure */
struct flir_fslp_cmd {
	u8 magic[2];
	u16 length;
	u8 data[];
} __packed;

/* Device State */
struct flir_boson_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_fwnode_endpoint ep;
	struct gpio_desc *reset_gpio;
	struct mutex lock;

	/* Format management */
	struct v4l2_mbus_framefmt fmt;
	const struct flir_boson_format *current_format;
	const struct flir_boson_framesize *current_framesize;

	/* MIPI state */
	u32 mipi_state;
	bool streaming;
	bool powered;

	/* Camera information */
	u32 camera_sn;
/* FSLP communication */
u8 fslp_tx_buf[FLIR_FSLP_MAX_DATA];
u8 fslp_rx_buf[FLIR_FSLP_MAX_DATA];
u32 command_count; /* Sequence number for commands */
};

/* IOCTL Interface */
struct flir_boson_ioctl_fslp {
	u32 tx_len;
	u32 rx_len;
	u8 data[FLIR_FSLP_MAX_DATA];
};

/* IOCTL Commands */
#define FLIR_BOSON_IOCTL_FSLP_FRAME   _IOWR('F', 0x01, struct flir_boson_ioctl_fslp)
#define FLIR_BOSON_IOCTL_GET_STATUS   _IOR('F', 0x03, u32)

/* Function prototypes - I2C Layer */
FLR_RESULT I2C_readFrame(struct flir_boson_dev *sensor, u8* readData, u32* readBytes);
FLR_RESULT I2C_writeFrame(struct flir_boson_dev *sensor, u8* writeData, u32 writeBytes);

/* Layer 3: Command Packagers (SDK-compatible API) */
FLR_RESULT flir_boson_send_int_cmd(struct flir_boson_dev *sensor, u32 cmd, u32 val, u32 delay_ms);
FLR_RESULT flir_boson_get_int_val(struct flir_boson_dev *sensor, u32 cmd, u32 *val);
FLR_RESULT flir_boson_get_dvo_muxtype(struct flir_boson_dev *sensor, FLR_DVOMUX_OUTPUT_IF_E output, FLR_DVOMUX_SOURCE_E *source, FLR_DVOMUX_TYPE_E *type);
FLR_RESULT flir_boson_set_dvo_muxtype(struct flir_boson_dev *sensor, FLR_DVOMUX_OUTPUT_IF_E output, FLR_DVOMUX_SOURCE_E source, FLR_DVOMUX_TYPE_E type);

/* Legacy compatibility */
// int flir_boson_fslp_send_frame(struct flir_boson_dev *sensor, const u8 *tx_data, u32 tx_len, u8 *rx_data, u32 rx_len);

/* Utility macros */
#define to_flir_boson_dev(sd) container_of(sd, struct flir_boson_dev, sd)

#endif /* FLIR_BOSON_H */
