/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FLIR Boson+ MIPI Camera V4L2 Driver
 * Copyright (C) 2024
 */

#ifndef FLIR_BOSON_H
#define FLIR_BOSON_H

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/gpio/consumer.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define FLIR_BOSON_NAME "flir-boson"
#define FLIR_BOSON_I2C_ADDR 0x6A

/* Simulation Mode - controlled via Makefile CFLAGS */

/* FSLP Protocol Constants */
#define FLIR_MAGIC_TOKEN_0    0x8E
#define FLIR_MAGIC_TOKEN_1    0xA1
#define FLIR_FSLP_HEADER_SIZE 4
#define FLIR_FSLP_MAX_DATA    256

/* FLIR SDK Command Codes */
#define DVO_SET_OUTPUT_INTERFACE   0x00060007
#define DVO_SET_TYPE               0x0006000F
#define DVO_SET_MIPI_STATE         0x00060024
#define DVO_APPLY_CUSTOM_SETTINGS  0x00060025
#define DVO_GET_MIPI_STATE         0x00060026

/* FLIR DVO Output Interface Types */
enum flir_dvo_output_interface {
	FLR_DVO_CMOS = 0,
	FLR_DVO_MIPI = 1,
};

/* FLIR DVO Types */
enum flir_dvo_type {
	FLR_DVO_TYPE_MONO8 = 0,
	FLR_DVO_TYPE_MONO14 = 1,
	FLR_DVO_TYPE_COLOR = 2,
	FLR_DVO_TYPE_MONO8MONO14 = 3,
	FLR_DVO_TYPE_COLORMONO14 = 4,
	FLR_DVO_TYPE_COLORMONO8 = 5,
};

/* MIPI State Machine */
enum flir_mipi_state {
	FLIR_MIPI_STATE_OFF = 0,
	FLIR_MIPI_STATE_PAUSED = 1,
	FLIR_MIPI_STATE_ACTIVE = 2,
};

/* Supported Formats */
struct flir_boson_format {
	u32 code;
	u32 flir_type;
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
	int mipi_state;
	bool streaming;
	bool powered;
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
#define FLIR_BOSON_IOCTL_POWER_STATE  _IOW('F', 0x02, int)
#define FLIR_BOSON_IOCTL_GET_STATUS   _IOR('F', 0x03, u32)

/* Function prototypes - Layer 1: I2C FSLP Framing (matches I2CFslp.py) */
int flir_fslp_send_frame(struct flir_boson_dev *sensor, u8 channel_id,
			 const u8 *payload, u32 payload_len);
int flir_fslp_read_frame(struct flir_boson_dev *sensor, u8 channel_id,
			 u8 *payload, u32 expected_len);

/* Layer 2: Command Dispatcher (matches Client_Dispatcher.py/c) */
int flir_command_dispatcher(struct flir_boson_dev *sensor, u32 seq_num, u32 fn_id,
			    const u8 *send_data, u32 send_bytes,
			    u8 *receive_data, u32 *receive_bytes);

/* Layer 3: Command Packagers (SDK-compatible API) */
int flir_boson_set_mipi_state(struct flir_boson_dev *sensor, int state);
int flir_boson_set_output_interface(struct flir_boson_dev *sensor, int interface);
int flir_boson_set_dvo_type(struct flir_boson_dev *sensor, u32 type);
int flir_boson_apply_settings(struct flir_boson_dev *sensor);
int flir_boson_get_mipi_state(struct flir_boson_dev *sensor, int *state);

/* Legacy compatibility */
int flir_boson_fslp_send_frame(struct flir_boson_dev *sensor,
			       const u8 *tx_data, u32 tx_len,
			       u8 *rx_data, u32 rx_len);

/* Utility macros */
#define to_flir_boson_dev(sd) container_of(sd, struct flir_boson_dev, sd)

#endif /* FLIR_BOSON_H */