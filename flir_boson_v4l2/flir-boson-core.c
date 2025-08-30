// SPDX-License-Identifier: GPL-2.0
/*
 * FLIR Boson+ MIPI Camera V4L2 Driver
 * Copyright (C) 2024
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "flir-boson.h"

/* Supported formats */
static const struct flir_boson_format flir_boson_formats[] = {
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.flir_type = FLR_DVO_TYPE_MONO8,
		.bpp = 8,
		.name = "RAW8",
	},
	{
		.code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.flir_type = FLR_DVO_TYPE_MONO14,
		.bpp = 14,
		.name = "RAW14",
	},
	{
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.flir_type = FLR_DVO_TYPE_COLOR,
		.bpp = 16,
		.name = "UYVY",
	},
};

/* Supported frame sizes */
static const struct flir_boson_framesize flir_boson_framesizes[] = {
	{ .width = 320, .height = 256, .max_fps = 60 },
	{ .width = 640, .height = 512, .max_fps = 60 },
};

#define FLIR_BOSON_NUM_FORMATS ARRAY_SIZE(flir_boson_formats)
#define FLIR_BOSON_NUM_FRAMESIZES ARRAY_SIZE(flir_boson_framesizes)

static const struct flir_boson_format *
flir_boson_find_format(u32 code)
{
	int i;

	for (i = 0; i < FLIR_BOSON_NUM_FORMATS; i++) {
		if (flir_boson_formats[i].code == code)
			return &flir_boson_formats[i];
	}

	return &flir_boson_formats[0]; /* Default to first format */
}

static const struct flir_boson_framesize *
flir_boson_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < FLIR_BOSON_NUM_FRAMESIZES; i++) {
		if (flir_boson_framesizes[i].width == width &&
		    flir_boson_framesizes[i].height == height)
			return &flir_boson_framesizes[i];
	}

	return &flir_boson_framesizes[0]; /* Default to first size */
}

/* V4L2 Subdev Core Operations */
static int flir_boson_s_power(struct v4l2_subdev *sd, int on)
{
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
	int ret = 0;

	dev_dbg(sensor->dev, "%s: power %s\n", __func__, on ? "on" : "off");

	mutex_lock(&sensor->lock);

	if (on && !sensor->powered) {
		if (sensor->reset_gpio) {
			gpiod_set_value_cansleep(sensor->reset_gpio, 0);
			msleep(10);
			gpiod_set_value_cansleep(sensor->reset_gpio, 1);
			msleep(100);
		}

		/* Wait for camera boot (2.5 seconds as per spec) */
		msleep(2500);

		/* Initialize MIPI interface */
		ret = flir_boson_set_output_interface(sensor, FLR_DVO_MIPI);
		if (ret) {
			dev_err(sensor->dev, "Failed to set MIPI interface: %d\n", ret);
			goto unlock;
		}

		sensor->powered = true;
		sensor->mipi_state = FLIR_MIPI_STATE_OFF;
	} else if (!on && sensor->powered) {
		/* Stop streaming if active */
		if (sensor->streaming) {
			flir_boson_set_mipi_state(sensor, FLIR_MIPI_STATE_OFF);
			sensor->streaming = false;
		}

		if (sensor->reset_gpio)
			gpiod_set_value_cansleep(sensor->reset_gpio, 0);

		sensor->powered = false;
	}

unlock:
	mutex_unlock(&sensor->lock);
	return ret;
}

static long flir_boson_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
	struct flir_boson_ioctl_fslp *fslp_cmd;
	int ret = 0;

	if (!sensor->powered)
		return -ENODEV;

	mutex_lock(&sensor->lock);

	switch (cmd) {
	case FLIR_BOSON_IOCTL_FSLP_CMD:
		fslp_cmd = (struct flir_boson_ioctl_fslp *)arg;
		ret = flir_boson_fslp_send_cmd(sensor, fslp_cmd->cmd_id,
					       fslp_cmd->data, fslp_cmd->tx_len,
					       fslp_cmd->data, fslp_cmd->rx_len);
		break;

	case FLIR_BOSON_IOCTL_POWER_STATE:
		ret = flir_boson_s_power(sd, *(int *)arg);
		break;

	case FLIR_BOSON_IOCTL_GET_STATUS:
		*(u32 *)arg = (sensor->powered ? 1 : 0) |
			      (sensor->streaming ? 2 : 0) |
			      (sensor->mipi_state << 2);
		break;

	default:
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&sensor->lock);
	return ret;
}

/* V4L2 Subdev Pad Operations */
static int flir_boson_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad != 0 || code->index >= FLIR_BOSON_NUM_FORMATS)
		return -EINVAL;

	code->code = flir_boson_formats[code->index].code;
	return 0;
}

static int flir_boson_enum_frame_size(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->pad != 0 || fse->index >= FLIR_BOSON_NUM_FRAMESIZES)
		return -EINVAL;

	if (!flir_boson_find_format(fse->code))
		return -EINVAL;

	fse->min_width = fse->max_width = flir_boson_framesizes[fse->index].width;
	fse->min_height = fse->max_height = flir_boson_framesizes[fse->index].height;

	return 0;
}

static int flir_boson_enum_frame_interval(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *sd_state,
					  struct v4l2_subdev_frame_interval_enum *fie)
{
	const struct flir_boson_framesize *framesize;

	if (fie->pad != 0 || fie->index > 0)
		return -EINVAL;

	if (!flir_boson_find_format(fie->code))
		return -EINVAL;

	framesize = flir_boson_find_framesize(fie->width, fie->height);
	if (!framesize)
		return -EINVAL;

	fie->interval.numerator = 1;
	fie->interval.denominator = framesize->max_fps;

	return 0;
}

static int flir_boson_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *format)
{
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);

	if (format->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);
	format->format = sensor->fmt;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int flir_boson_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *format)
{
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
	const struct flir_boson_format *new_format;
	const struct flir_boson_framesize *new_framesize;
	int ret = 0;

	if (format->pad != 0)
		return -EINVAL;

	new_format = flir_boson_find_format(format->format.code);
	new_framesize = flir_boson_find_framesize(format->format.width,
						  format->format.height);

	mutex_lock(&sensor->lock);

	/* Don't change format while streaming */
	if (sensor->streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Apply format if powered and different from current */
	if (sensor->powered &&
	    (new_format != sensor->current_format ||
	     new_framesize != sensor->current_framesize)) {

		/* Set MIPI state to OFF before changing format */
		ret = flir_boson_set_mipi_state(sensor, FLIR_MIPI_STATE_OFF);
		if (ret)
			goto unlock;

		/* Set new DVO type */
		ret = flir_boson_set_dvo_type(sensor, new_format->flir_type);
		if (ret)
			goto unlock;

		/* Apply settings */
		ret = flir_boson_apply_settings(sensor);
		if (ret)
			goto unlock;

		sensor->current_format = new_format;
		sensor->current_framesize = new_framesize;
	}

	/* Update format structure */
	sensor->fmt.code = new_format->code;
	sensor->fmt.width = new_framesize->width;
	sensor->fmt.height = new_framesize->height;
	sensor->fmt.field = V4L2_FIELD_NONE;
	sensor->fmt.colorspace = V4L2_COLORSPACE_RAW;

	format->format = sensor->fmt;

unlock:
	mutex_unlock(&sensor->lock);
	return ret;
}

/* V4L2 Subdev Video Operations */
static int flir_boson_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
	int ret = 0;

	dev_dbg(sensor->dev, "%s: stream %s\n", __func__, enable ? "on" : "off");

	mutex_lock(&sensor->lock);

	if (!sensor->powered) {
		ret = -ENODEV;
		goto unlock;
	}

	if (enable && !sensor->streaming) {
		/* Start streaming */
		ret = flir_boson_set_mipi_state(sensor, FLIR_MIPI_STATE_ACTIVE);
		if (ret) {
			dev_err(sensor->dev, "Failed to start MIPI: %d\n", ret);
			goto unlock;
		}
		sensor->streaming = true;
	} else if (!enable && sensor->streaming) {
		/* Stop streaming */
		ret = flir_boson_set_mipi_state(sensor, FLIR_MIPI_STATE_OFF);
		if (ret) {
			dev_err(sensor->dev, "Failed to stop MIPI: %d\n", ret);
			goto unlock;
		}
		sensor->streaming = false;
	}

unlock:
	mutex_unlock(&sensor->lock);
	return ret;
}

/* V4L2 Subdev Operations */
static const struct v4l2_subdev_core_ops flir_boson_core_ops = {
	.s_power = flir_boson_s_power,
	.ioctl = flir_boson_ioctl,
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops flir_boson_video_ops = {
	.s_stream = flir_boson_s_stream,
};

static const struct v4l2_subdev_pad_ops flir_boson_pad_ops = {
	.enum_mbus_code = flir_boson_enum_mbus_code,
	.get_fmt = flir_boson_get_fmt,
	.set_fmt = flir_boson_set_fmt,
	.enum_frame_size = flir_boson_enum_frame_size,
	.enum_frame_interval = flir_boson_enum_frame_interval,
};

static const struct v4l2_subdev_ops flir_boson_subdev_ops = {
	.core = &flir_boson_core_ops,
	.video = &flir_boson_video_ops,
	.pad = &flir_boson_pad_ops,
};

/* Media Entity Operations */
static int flir_boson_link_setup(struct media_entity *entity,
				  const struct media_pad *local,
				  const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations flir_boson_media_ops = {
	.link_setup = flir_boson_link_setup,
};

/* I2C Driver Functions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int flir_boson_probe(struct i2c_client *client)
#else
static int flir_boson_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct flir_boson_dev *sensor;
	int ret;

	dev_info(dev, "FLIR Boson+ MIPI camera driver probing\n");

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->dev = dev;
	sensor->i2c_client = client;
	mutex_init(&sensor->lock);

	/* Initialize default format */
	sensor->current_format = &flir_boson_formats[0];
	sensor->current_framesize = &flir_boson_framesizes[1]; /* 640x512 */
	sensor->fmt.code = sensor->current_format->code;
	sensor->fmt.width = sensor->current_framesize->width;
	sensor->fmt.height = sensor->current_framesize->height;
	sensor->fmt.field = V4L2_FIELD_NONE;
	sensor->fmt.colorspace = V4L2_COLORSPACE_RAW;

	/* Get reset GPIO */
	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset_gpio)) {
		ret = PTR_ERR(sensor->reset_gpio);
		dev_warn(dev, "Cannot get reset GPIO: %d\n", ret);
		sensor->reset_gpio = NULL;
	}

	/* Parse device tree endpoint */
	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "Endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &sensor->ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Could not parse endpoint\n");
		return ret;
	}

	if (sensor->ep.bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(dev, "Unsupported bus type %d\n", sensor->ep.bus_type);
		return -EINVAL;
	}

	/* Initialize V4L2 subdev */
	v4l2_i2c_subdev_init(&sensor->sd, client, &flir_boson_subdev_ops);
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->sd.entity.ops = &flir_boson_media_ops;

	/* Initialize media pad */
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret) {
		dev_err(dev, "Could not init media entity\n");
		goto cleanup_mutex;
	}

	/* Register V4L2 subdev */
	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret) {
		dev_err(dev, "Could not register v4l2 device: %d\n", ret);
		goto cleanup_entity;
	}

	dev_info(dev, "FLIR Boson+ MIPI camera driver loaded\n");
	return 0;

cleanup_entity:
	media_entity_cleanup(&sensor->sd.entity);
cleanup_mutex:
	mutex_destroy(&sensor->lock);
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
static int flir_boson_remove(struct i2c_client *client)
#else
static void flir_boson_remove(struct i2c_client *client)
#endif
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);

	v4l2_async_unregister_subdev(&sensor->sd);
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
	return 0;
#endif
}

static const struct i2c_device_id flir_boson_id[] = {
	{"flir-boson", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, flir_boson_id);

static const struct of_device_id flir_boson_dt_ids[] = {
	{ .compatible = "flir,boson-mipi" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, flir_boson_dt_ids);

static struct i2c_driver flir_boson_i2c_driver = {
	.driver = {
		.name = FLIR_BOSON_NAME,
		.of_match_table = flir_boson_dt_ids,
	},
	.id_table = flir_boson_id,
	.probe = flir_boson_probe,
	.remove = flir_boson_remove,
};

module_i2c_driver(flir_boson_i2c_driver);

MODULE_DESCRIPTION("FLIR Boson+ MIPI Camera V4L2 Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FLIR Driver Development");
MODULE_VERSION("1.0");