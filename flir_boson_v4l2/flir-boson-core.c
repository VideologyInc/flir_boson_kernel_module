// SPDX-License-Identifier: MIT
/*
 * FLIR Boson+ MIPI Camera V4L2 Driver
 * Copyright (C) 2025 VideologyInc
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/videodev2.h>

#include <linux/version.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "EnumTypes.h"
#include "FunctionCodes.h"
#include "flir-boson.h"

static int enable_radiometry = 1;
module_param(enable_radiometry, int, 0644);
MODULE_PARM_DESC(enable_radiometry, "enable_radiometry");

/**
 * flr_result_to_errno - Convert FLR_RESULT to Linux error code
 * @result: FLR_RESULT code from SDK
 * @return: Corresponding Linux error code
 */
static int flr_result_to_errno(FLR_RESULT result)
{
	switch (result) {
	case R_SUCCESS: /* FLR_OK and FLR_COMM_OK have same value */
		return 0;
	case FLR_BAD_ARG_POINTER_ERROR:
	case R_CAM_API_INVALID_INPUT:
		return -EINVAL;
	case FLR_COMM_TIMEOUT_ERROR: /* FLR_COMM_ERROR_WRITING_COMM has same value */
	case FLR_COMM_ERROR_READING_COMM:
		return -EIO;
	case FLR_NOT_READY:
	case FLR_CAM_BUSY:
		return -EBUSY;
	case FLR_RANGE_ERROR:
	case FLR_DATA_SIZE_ERROR:
		return -ERANGE;
	case R_SDK_PKG_BUFFER_OVERFLOW:
	case R_CAM_PKG_BUFFER_OVERFLOW:
		return -ENOSPC;
	case FLR_COMM_PORT_NOT_OPEN:
	case FLR_COMM_NO_DEV:
		return -ENODEV;
	case R_SDK_DSPCH_SEQUENCE_MISMATCH:
	case R_SDK_DSPCH_ID_MISMATCH:
		return -EPROTO;
	default:
		return -EREMOTEIO;
	}
}

/* Works with FOUR-CC values: 'GREY', 'NV12', 'Y16 ', and via CSC: RGB3  */
/* Supported formats */
static const struct flir_boson_format flir_boson_formats[] = {
    // leave YUV format default. The color-scapce-conversion knows how to handle it.
    {
        .code          = MEDIA_BUS_FMT_UYVY8_1X16,
        .flir_type     = FLR_DVO_TYPE_COLOR,
		.flir_mux_type = FLR_DVOMUX_TYPE_COLOR,
        .name          = "UYVY",
    },
    {
        .code          = MEDIA_BUS_FMT_Y14_1X14,
        .flir_type     = FLR_DVO_TYPE_MONO14,
        .flir_mux_type = FLR_DVOMUX_TYPE_MONO16,
        .name          = "RAW14",
    },
    {
        .code          = MEDIA_BUS_FMT_Y8_1X8,
        .flir_type     = FLR_DVO_TYPE_MONO8,
        .flir_mux_type = FLR_DVOMUX_TYPE_MONO8,
        .name          = "RAW8",
	},
};

/* Supported frame sizes */
static const struct flir_boson_framesize flir_boson_framesizes[] = {
    {.width = 320, .height = 256, .max_fps = 60},
    {.width = 640, .height = 512, .max_fps = 60},
    {.width = 640, .height = 514, .max_fps = 60}, // add telementry Line
};

#define FLIR_BOSON_NUM_FORMATS ARRAY_SIZE(flir_boson_formats)
#define FLIR_BOSON_NUM_FRAMESIZES ARRAY_SIZE(flir_boson_framesizes)

static const struct flir_boson_format *flir_boson_find_format(struct flir_boson_dev *sensor, u32 code) {
	int i;
    u32 code_search = code;

    switch (code) {
    case MEDIA_BUS_FMT_YUYV8_2X8:
    case MEDIA_BUS_FMT_YVYU8_2X8:
    case MEDIA_BUS_FMT_UYVY8_2X8:
    case MEDIA_BUS_FMT_UYVY8_1X16:
    case MEDIA_BUS_FMT_VYUY8_2X8:
    case MEDIA_BUS_FMT_VYUY8_1X16:
    case MEDIA_BUS_FMT_YUYV8_1X16: code_search = MEDIA_BUS_FMT_UYVY8_1X16; break;
    case MEDIA_BUS_FMT_Y10_1X10:
    case MEDIA_BUS_FMT_Y12_1X12:
    case MEDIA_BUS_FMT_Y16_1X16: code_search = MEDIA_BUS_FMT_Y14_1X14; break;
    default: code_search = code;
    }
	for (i = 0; i < FLIR_BOSON_NUM_FORMATS; i++) {
        if (flir_boson_formats[i].code == code_search) return &flir_boson_formats[i];
	}

    dev_dbg(sensor->dev, "FORMAT: Unsupported format code 0x%08X. returning default", code);
	return &flir_boson_formats[0]; /* Default to first format */
}

static const struct flir_boson_framesize *flir_boson_find_framesize(u32 width, u32 height) {
	int i;

	for (i = 0; i < FLIR_BOSON_NUM_FRAMESIZES; i++) {
        if (flir_boson_framesizes[i].width == width && flir_boson_framesizes[i].height == height) return &flir_boson_framesizes[i];
	}

    return &flir_boson_framesizes[1]; /* Default to 640x512 */
}

/* V4L2 Subdev Core Operations */
static int flir_boson_s_power(struct v4l2_subdev *sd, int on) {
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
    FLR_RESULT             ret    = R_SUCCESS;

	dev_dbg(sensor->dev, "%s: power %s\n", __func__, on ? "on" : "off");

	mutex_lock(&sensor->lock);

	if (on && !sensor->powered) {
		// if (sensor->reset_gpio) {
		// 	gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		// 	msleep(10);
		// 	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
		// 	msleep(100);
		// }

		// /* Wait for camera boot (2.5 seconds as per spec) */
		// msleep(2500);

		/* Initialize MIPI interface */
		dev_dbg(sensor->dev, "POWER: Setting output interface to MIPI");
        if (ret == R_SUCCESS) ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_OFF, 1);
        if (ret == R_SUCCESS) ret = flir_boson_send_int_cmd(sensor, DVO_SETTYPE, sensor->current_format->flir_type, 100);
		if (ret == R_SUCCESS)
            ret =
                flir_boson_send_int_cmd(sensor, DVO_SETOUTPUTFORMAT, sensor->current_format->flir_type == FLR_DVO_TYPE_COLOR ? FLR_DVO_YCBCR : FLR_DVO_IR16, 1);
        if (ret == R_SUCCESS) ret = flir_boson_send_int_cmd(sensor, DVO_SETOUTPUTINTERFACE, FLR_DVO_MIPI, 100);

		if (ret != R_SUCCESS) {
			dev_err(sensor->dev, "Failed to set MIPI interface: %s\n", flr_result_to_string(ret));
			ret = flr_result_to_errno(ret);
            goto unlock;
		}
		dev_dbg(sensor->dev, "POWER: Output interface set to MIPI successfully");
		sensor->powered = 1;
        ret             = flir_boson_get_int_val(sensor, DVO_GETMIPISTATE, &sensor->mipi_state);
        if (ret != R_SUCCESS) dev_warn(sensor->dev, "Failed to get MIPI state: %s", flr_result_to_string(ret));
	} else if (!on && sensor->powered) {
		/* Stop streaming if active */
		if (sensor->streaming) {
			dev_dbg(sensor->dev, "POWER: Stopping streaming during power down");
			ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_OFF, 1);
            if (ret != R_SUCCESS) dev_warn(sensor->dev, "Failed to stop MIPI during power down: %s", flr_result_to_string(ret));
			sensor->streaming = 0;
			dev_dbg(sensor->dev, "POWER: Streaming stopped");
		}

		// if (sensor->reset_gpio)
		// 	gpiod_set_value_cansleep(sensor->reset_gpio, 0);

		// sensor->powered = false;
	}

unlock:
	mutex_unlock(&sensor->lock);
	return ret == R_SUCCESS ? 0 : flr_result_to_errno(ret);
}

/* V4L2 Subdev Video Operations */
static int flir_boson_s_stream_priv(struct flir_boson_dev *sensor, int enable) {
    FLR_RESULT ret = R_SUCCESS;
    u32        state;

    dev_dbg(sensor->dev, "%s: stream %s\n", __func__, enable ? "on" : "off");
    dev_dbg(sensor->dev, "STREAM: Current state - powered=%d, streaming=%d, mipi_state=%d, en=%d", sensor->powered, sensor->streaming, sensor->mipi_state,
            enable);

    mutex_lock(&sensor->lock);

    if (enable && !sensor->streaming) {
        dev_dbg(sensor->dev, "STREAM: Starting streaming - setting MIPI to ACTIVE");
        ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPICLOCKLANEMODE, FLR_DVO_MIPI_CLOCK_LANE_MODE_CONTINUOUS, 1);
        /* Start streaming */
        dev_dbg(sensor->dev, "STREAM: Starting streaming - setting MIPI to ACTIVE");
        ret |= flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_ACTIVE, 400);
        if (ret != R_SUCCESS) {
            dev_err(sensor->dev, "Failed to start MIPI: %s\n", flr_result_to_string(ret));
            goto unlock;
        } else {
            sensor->streaming = true;
            dev_dbg(sensor->dev, "STREAM: Streaming started successfully");
            ret = flir_boson_get_int_val(sensor, DVO_GETMIPISTATE, &state);
            dev_dbg(sensor->dev, "mipi State: %d", state);
        }
    } else if (!enable && sensor->streaming) {
        /* Stop streaming */
        dev_dbg(sensor->dev, "STREAM: Stopping streaming - setting MIPI to OFF");
        ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_OFF, 1);
        if (ret != R_SUCCESS) {
            dev_err(sensor->dev, "Failed to stop MIPI: %s\n", flr_result_to_string(ret));
            goto unlock;
        }
        sensor->streaming = false;
        dev_dbg(sensor->dev, "STREAM: Streaming stopped successfully");
    }

unlock:
    mutex_unlock(&sensor->lock);
    return ret == R_SUCCESS ? 0 : flr_result_to_errno(ret);
}

/* V4L2 Subdev Video Operations */
static int flir_boson_s_stream(struct v4l2_subdev *sd, int enable) {
    struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
    return flir_boson_s_stream_priv(sensor, enable);
}

/* V4L2 Subdev Pad Operations */
static int flir_boson_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_mbus_code_enum *code) {
    if (code->pad != 0 || code->index >= FLIR_BOSON_NUM_FORMATS) return -EINVAL;

	dev_dbg(to_flir_boson_dev(sd)->dev, "ENUM_MBUS_CODE: index=%u", code->index);
	code->code = flir_boson_formats[code->index].code;
	return 0;
}

static int flir_boson_enum_frame_size(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_frame_size_enum *fse) {
    if (fse->pad != 0 || fse->index >= FLIR_BOSON_NUM_FRAMESIZES) return -EINVAL;

    if (!flir_boson_find_format(to_flir_boson_dev(sd), fse->code)) return -EINVAL;

	dev_dbg(to_flir_boson_dev(sd)->dev, "ENUM_FRAME_SIZE: index=%u", fse->index);
	fse->min_width = fse->max_width = flir_boson_framesizes[fse->index].width;
	fse->min_height = fse->max_height = flir_boson_framesizes[fse->index].height;

	return 0;
}

static int flir_boson_enum_frame_interval(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_frame_interval_enum *fie) {
	const struct flir_boson_framesize *framesize;

    if (fie->pad != 0 || fie->index > 0) return -EINVAL;

    if (!flir_boson_find_format(to_flir_boson_dev(sd), fie->code)) return -EINVAL;

	framesize = flir_boson_find_framesize(fie->width, fie->height);
    if (!framesize) return -EINVAL;

	dev_dbg(to_flir_boson_dev(sd)->dev, "ENUM_FRAME_INTERVAL: width=%u, height=%u", fie->width, fie->height);
    fie->interval.numerator   = 1;
	fie->interval.denominator = framesize->max_fps;

	return 0;
}

static int flir_boson_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_format *format) {
	struct flir_boson_dev *sensor = to_flir_boson_dev(sd);

    if (format->pad != 0) return -EINVAL;

    format->format = sensor->fmt;

	dev_dbg(sensor->dev, "FORMAT: Getting current format - powered=%d, streaming=%d", sensor->powered, sensor->streaming);
    dev_dbg(sensor->dev, "FORMAT: Getting format - code=0x%08X, width=%u, height=%u, color=%d", format->format.code, format->format.width,
            format->format.height, format->format.colorspace);

	return 0;
}

static int flir_set_agc_paramaters(struct flir_boson_dev * sensor)
{
	// set agc parameters.
	union fdata { float f; u32 u; };
	union fdata plateau; 
	plateau.f = 3.0f;

	union fdata linear_percent;
	linear_percent.f = 10.0f;

	union fdata gain;
	gain.f = 8.0f;

	union fdata gamma;
	gamma.f = 0.5f;

	union fdata outlier;
	outlier.f = 0.01f;

	union fdata dde;
	dde.f = 3.0f;

    FLR_RESULT ret = R_SUCCESS;

	ret = flir_boson_send_int_cmd(sensor, AGC_SETPERCENTPERBIN, plateau.u, 1) &		// plateau value [1, 100, 7]
		flir_boson_send_int_cmd(sensor, AGC_SETLINEARPERCENT, linear_percent.u, 1) &		// linear percent [1, 100, 20]
		flir_boson_send_int_cmd(sensor, AGC_SETMAXGAIN, gain.u, 1) &		// max gain [0.25, 8.00, 1.25]
		flir_boson_send_int_cmd(sensor, AGC_SETGAMMA, gamma.u, 1) &		// ace = gamma [0.5, 4.00, 0.9]
		flir_boson_send_int_cmd(sensor, AGC_SETOUTLIERCUT, outlier.u, 1) &	// tail = outlier [0.0, 49.0, 0.0]
		flir_boson_send_int_cmd(sensor, AGC_SETD2BR, dde.u, 1) &		// dde = details to background ratio [0.0, 6.00, 1.3]
		flir_boson_send_int_cmd(sensor, AGC_SETUSEENTROPY, FLR_ENABLE, 1) &		// use entropy = FLR_ENABLE, use platueau = FLR_DISABLE
		flir_boson_send_int_cmd(sensor, AGC_SETBRIGHTNESS, 128, 1);		// brightness [0, 255], default = 128

	if (ret != R_SUCCESS) {
        dev_err(sensor->dev, "FORMAT: Failed to set AGC parameters: %s", flr_result_to_string(ret));
        ret = flr_result_to_errno(ret);
    }

	return ret;
}

static int flir_get_agc_paramaters(struct flir_boson_dev * sensor)
{
    FLR_RESULT ret = R_SUCCESS;
	union fdata { float f; u32 u; };

	union fdata perc_per_bin, lin_perc, outlier, drout, maxgain, damping, gamma, 
		detailhead, d2br, gmax, gmin;
	u32 bin_first, bin_last, entropy, agc_mode, brightness, radius;

	ret = flir_boson_get_int_val(sensor, AGC_GETMODE, &agc_mode);	// AGC mode
	ret &= flir_boson_get_int_val(sensor, AGC_GETUSEENTROPY, &entropy);	// 1 = use entropy, 1 = use plateau

	ret &= flir_boson_get_int_val(sensor, AGC_GETOUTLIERCUT, &outlier.u);	// outlier cut = tail rejection in percentage
	ret &= flir_boson_get_int_val(sensor, AGC_GETMAXGAIN, &maxgain.u);	// max gain (gain limit per bin)
	ret &= flir_boson_get_int_val(sensor, AGC_GETDF, &damping.u);	// damping factor
	ret &= flir_boson_get_int_val(sensor, AGC_GETGAMMA, &gamma.u);	// gamma = ace
	ret &= flir_boson_get_int_val(sensor, AGC_GETPERCENTPERBIN, &perc_per_bin.u);	// percent per bin = plateau value
	ret &= flir_boson_get_int_val(sensor, AGC_GETLINEARPERCENT, &lin_perc.u);	// linear percent

	ret &= flir_boson_get_int_val(sensor, AGC_GETDETAILHEADROOM, &detailhead.u);	// DDE detail head room
	ret &= flir_boson_get_int_val(sensor, AGC_GETD2BR, &d2br.u);	// DDE detail to background ratio

	ret &= flir_boson_get_int_val(sensor, AGC_GETDROUT, &drout.u);	// output dynamic range
	ret &= flir_boson_get_int_val(sensor, AGC_GETFIRSTBIN, &bin_first);	// first bin
	ret &= flir_boson_get_int_val(sensor, AGC_GETLASTBIN, &bin_last);	// last bin

	union word_data { u16 th[2]; u32 u;};
	union word_data tf_thresholds;

	ret &= flir_boson_get_int_val(sensor, AGC_GETTFTHRESHOLDS, &tf_thresholds.u);	// thresholds in threshold mode.
	ret &= flir_boson_get_int_val(sensor, AGC_GETBRIGHTNESS, &brightness);	// brightness

	ret &= flir_boson_get_int_val(sensor, AGC_GETRADIUS, &radius);	// DDE sharpening radius
	ret &= flir_boson_get_int_val(sensor, AGC_GETGMAX, &gmax.u);	// DDE sharpening excluding noise
	ret &= flir_boson_get_int_val(sensor, AGC_GETGMIN, &gmin.u);	// DDE noise supression

	if (ret != R_SUCCESS) {
        dev_err(sensor->dev, "FORMAT: Failed to get AGC parameters: %s", flr_result_to_string(ret));
        return flr_result_to_errno(ret);
    }

	char *message[] = {"normal", "hold", "threshold", "auto bright", "auto linear", "manual"};
	pr_info("AGC mode    = %s \n", message[agc_mode]);
	pr_info("Use Entropy = %d \n", entropy);

	// Same order as AGC Presets panel of the Boson Windows GUI ;-) 6 + 2 attributes or parameters
	pr_info("Tail Outlier Percent     = %#08X \n", outlier.u);
	pr_info("Max Gain                 = %#08X \n", maxgain.u);
	pr_info("Damping Factor           = %#08X \n", damping.u);
	pr_info("ACE Gamma                = %#08X \n", gamma.u);
	pr_info("Plateau Percent          = %#08X \n", perc_per_bin.u);
	pr_info("Linear Percent           = %#08X \n", lin_perc.u);

	pr_info("DDE Detail Head Room     = %#08X \n", detailhead.u);
	pr_info("DDE detail to background = %#08X \n", d2br.u);

	// Other important parameters.
	pr_info("Output Dynamic Range     = %#08X \n", drout.u);
	pr_info("(first bin, last bin)    = (%d, %d)\n", bin_first, bin_last);
	pr_info("Tf Thresholds            = (%d, %d)\n", tf_thresholds.th[0], tf_thresholds.th[1]);

	pr_info("Brightness               = %d \n", brightness);

	// Other DDE parameters (sharpening and noise suppression etc.)
	pr_info("DDE Object Radius        = %d \n", radius);
	pr_info("DDE details sharpening   = %#08X \n", gmax.u);
	pr_info("DDE noise suppresion     = %#08X \n", gmin.u);

	return ret;
}


static int flir_boson_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_format *format) {
    struct flir_boson_dev             *sensor = to_flir_boson_dev(sd);
    const struct flir_boson_format    *new_format;
	const struct flir_boson_framesize *new_framesize;
    FLR_RESULT                         ret = R_SUCCESS;

    if (format->pad != 0) return -EINVAL;

    new_format    = flir_boson_find_format(sensor, format->format.code);
	new_framesize = flir_boson_find_framesize(format->format.width, format->format.height);

	dev_dbg(sensor->dev, "FORMAT: Setting format - code=0x%08X, width=%u, height=%u", format->format.code, format->format.width, format->format.height);
	dev_dbg(sensor->dev, "FORMAT: New format type=%u, current powered=%d, streaming=%d", new_format->flir_type, sensor->powered, sensor->streaming);

    if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
        struct v4l2_mbus_framefmt *try_fmt = v4l2_subdev_get_try_format(sd, sd_state, format->pad);
        try_fmt->code                      = new_format->code;
        try_fmt->width                     = new_framesize->width;
        try_fmt->height                    = new_framesize->height;
        try_fmt->field                     = V4L2_FIELD_NONE;
        try_fmt->colorspace                = new_format->flir_type == FLR_DVO_TYPE_COLOR ? V4L2_COLORSPACE_SRGB : V4L2_COLORSPACE_RAW;
        try_fmt->xfer_func                 = V4L2_XFER_FUNC_NONE;
        try_fmt->ycbcr_enc                 = V4L2_YCBCR_ENC_DEFAULT;
        try_fmt->quantization              = V4L2_QUANTIZATION_DEFAULT;
        return 0;
    }

	mutex_lock(&sensor->lock);

	/* Don't change format while streaming */
	if (sensor->streaming) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Apply format if necessary and different from current */
	if (1) { // sensor->powered && (new_format != sensor->current_format || new_framesize != sensor->current_framesize)) {

		dev_dbg(sensor->dev, "FORMAT: Format change required - applying new settings");

		/* Set MIPI state to OFF before changing format */
		dev_dbg(sensor->dev, "FORMAT: Setting MIPI to OFF before format change");
		ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_OFF, 1);
		if (ret != R_SUCCESS) {
			dev_err(sensor->dev, "FORMAT: Failed to set MIPI OFF: %s", flr_result_to_string(ret));
			ret = flr_result_to_errno(ret);
			goto unlock;
		}
        /* Add telemetry line */
        if (format->format.height >= 512) {
            dev_dbg(sensor->dev, "FORMAT: Adding telemetry line");
            ret = flir_boson_send_int_cmd(sensor, TELEMETRY_SETSTATE, FLR_ENABLE, 1);
            ret = flir_boson_send_int_cmd(sensor, TELEMETRY_SETLOCATION, FLR_TELEMETRY_LOC_BOTTOM, 1);
            ret = flir_boson_send_int_cmd(sensor, TELEMETRY_SETMIPIEMBEDDEDDATATAG, FLR_DISABLE, 1);
        } else {
            dev_dbg(sensor->dev, "FORMAT: Removing telemetry line");
            ret = flir_boson_send_int_cmd(sensor, TELEMETRY_SETSTATE, FLR_DISABLE, 1);
        }
		/* Set new DVO type */
		dev_dbg(sensor->dev, "FORMAT: Setting DVO type to mipi");
		ret = flir_boson_send_int_cmd(sensor, DVO_SETTYPE, new_format->flir_type, 100);
		if (ret != R_SUCCESS) {
			dev_err(sensor->dev, "FORMAT: Failed to set DVO type: %s", flr_result_to_string(ret));
			ret = flr_result_to_errno(ret);
            goto unlock;
		}

		/* Set new DVO output format */
		u32 outformat = FLR_DVO_DEFAULT_FORMAT;
        outformat     = (new_format->flir_type == FLR_DVO_TYPE_COLOR) ? FLR_DVO_YCBCR : FLR_DVO_IR16;
		dev_dbg(sensor->dev, "FORMAT: Setting DVO output-format to %d", outformat);
		ret = flir_boson_send_int_cmd(sensor, DVO_SETOUTPUTFORMAT, outformat, 1);
		if (ret != R_SUCCESS) {
			dev_err(sensor->dev, "FORMAT: Failed to set DVO output-format: %s", flr_result_to_string(ret));
            ret = flr_result_to_errno(ret);
            goto unlock;
        }

		// Get and show current AGC parameters.
		ret = flir_get_agc_paramaters(sensor);

        /* Setup Linear radiometric mode */
        /* from https://flir.custhelp.com/app/answers/detail/a_id/3387/~/flir-oem---boson-video-and-image-capture-using-opencv-16-bit-y16 */
        if ((new_format->code == MEDIA_BUS_FMT_Y14_1X14) && (enable_radiometry)) {

			ret = flir_boson_send_int_cmd(sensor, BOSON_SETGAINMODE, FLR_BOSON_AUTO_GAIN, 1); //  FLR_BOSON_HIGH_GAIN
            if (ret != R_SUCCESS) {
                dev_err(sensor->dev, "FORMAT: Failed to set gain mode: %s", flr_result_to_string(ret));
                ret = flr_result_to_errno(ret);
                // goto unlock;
            }

			// newly added set AGC mode: auto bright or auto linear
			ret = flir_boson_send_int_cmd(sensor, AGC_SETMODE, FLR_AGC_MODE_NORMAL, 1); // FLR_AGC_MODE_AUTO_BRIGHT // FLR_AGC_MODE_AUTO_LINEAR

			if (ret != R_SUCCESS) {
                dev_err(sensor->dev, "FORMAT: Failed to set AGC mode: %s", flr_result_to_string(ret));
                ret = flr_result_to_errno(ret);
                // goto unlock;
            }

			// set default AGC parameters.
			// ret = flir_set_agc_paramaters(sensor);

			/*	
			ret = flir_boson_send_int_cmd(sensor, TLINEAR_SETCONTROL, FLR_ENABLE, 1);
            if (ret != R_SUCCESS) {
                dev_err(sensor->dev, "FORMAT: Failed to enable linear mode: %s", flr_result_to_string(ret));
                ret = flr_result_to_errno(ret);
                // goto unlock;
            }
            ret = flir_boson_send_int_cmd(sensor, RADIOMETRY_SETTRANSMISSIONWINDOW, 100, 1);
            if (ret != R_SUCCESS) {
                dev_err(sensor->dev, "FORMAT: Failed to set transmission window: %s", flr_result_to_string(ret));
                ret = flr_result_to_errno(ret);
                // goto unlock;
            }
            ret = flir_boson_send_int_cmd(sensor, TLINEAR_REFRESHLUT, FLR_BOSON_HIGH_GAIN, 1);
            if (ret != R_SUCCESS) {
                dev_err(sensor->dev, "FORMAT: Failed to refresh LUT: %s", flr_result_to_string(ret));
                ret = flr_result_to_errno(ret);
                // goto unlock;
            }
			*/
            u32 dummy;
            ret = flir_boson_get_int_val(sensor, BOSON_RUNFFC, &dummy);
            if (ret != R_SUCCESS) {
                dev_err(sensor->dev, "FORMAT: Failed to run FFC: %s", flr_result_to_string(ret));
			ret = flr_result_to_errno(ret);
			// goto unlock;
            }
		}

		flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_OFF, 1);

		/* Set new DVO muxtype */
		dev_dbg(sensor->dev, "FORMAT: Setting DVO muxtype to mipi and %d", new_format->flir_mux_type);
		ret = flir_boson_set_dvo_muxtype(sensor, FLR_DVOMUX_OUTPUT_IF_MIPITX, FLR_DVOMUX_SRC_IR, (FLR_DVOMUX_TYPE_E)new_format->flir_mux_type);
		if (ret != R_SUCCESS) {
			dev_err(sensor->dev, "FORMAT: Failed to set DVO muxtype: %s", flr_result_to_string(ret));
			ret = flr_result_to_errno(ret);
            goto unlock;
		}

        sensor->current_format    = new_format;
		sensor->current_framesize = new_framesize;
		dev_dbg(sensor->dev, "FORMAT: Format change completed successfully");
	} else {
		dev_dbg(sensor->dev, "FORMAT: No format change needed");
	}

	/* Update format structure */
    sensor->fmt.code       = new_format->code;
    sensor->fmt.width      = new_framesize->width;
    sensor->fmt.height     = new_framesize->height;
    sensor->fmt.field      = V4L2_FIELD_NONE;
	sensor->fmt.colorspace = new_format->flir_type == FLR_DVO_TYPE_COLOR ? V4L2_COLORSPACE_SRGB : V4L2_COLORSPACE_RAW;

	format->format = sensor->fmt;

unlock:
	mutex_unlock(&sensor->lock);
    return ret == R_SUCCESS ? 0 : (ret > 0 ? ret : flr_result_to_errno(ret));
}

/* V4L2 Subdev Operations */
static const struct v4l2_subdev_core_ops flir_boson_core_ops = {
	.s_power = flir_boson_s_power,
};

static const struct v4l2_subdev_video_ops flir_boson_video_ops = {
	.s_stream = flir_boson_s_stream,
};

static const struct v4l2_subdev_pad_ops flir_boson_pad_ops = {
    .enum_mbus_code      = flir_boson_enum_mbus_code,
    .get_fmt             = flir_boson_get_fmt,
    .set_fmt             = flir_boson_set_fmt,
    .enum_frame_size     = flir_boson_enum_frame_size,
	.enum_frame_interval = flir_boson_enum_frame_interval,
};

static const struct v4l2_subdev_ops flir_boson_subdev_ops = {
    .core  = &flir_boson_core_ops,
	.video = &flir_boson_video_ops,
    .pad   = &flir_boson_pad_ops,
};

/* Media Entity Operations */
static int flir_boson_link_setup(struct media_entity *entity, const struct media_pad *local, const struct media_pad *remote, u32 flags) { return 0; }

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
    struct device         *dev = &client->dev;
    struct fwnode_handle  *endpoint;
	struct flir_boson_dev *sensor;
    int                    ret;

	pr_info("***** AB1969 Boson Flir Probe starts *****\n");

	dev_info(dev, "FLIR Boson+ MIPI camera driver probing\n");
	dev_dbg(dev, "PROBE: I2C address=0x%02x", client->addr);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
    if (!sensor) return -ENOMEM;

    sensor->dev           = dev;
    sensor->i2c_client    = client;
	sensor->command_count = get_random_u32() >> 23;
	mutex_init(&sensor->lock);
	dev_dbg(dev, "PROBE: Device structure initialized");

	/* Initialize default format */
    sensor->current_format    = &flir_boson_formats[0];
	sensor->current_framesize = &flir_boson_framesizes[1]; /* 640x512 */
    sensor->fmt.code          = sensor->current_format->code;
    sensor->fmt.width         = sensor->current_framesize->width;
    sensor->fmt.height        = sensor->current_framesize->height;
    sensor->fmt.field         = V4L2_FIELD_NONE;
    sensor->fmt.colorspace    = V4L2_COLORSPACE_DEFAULT;
    sensor->fmt.ycbcr_enc     = V4L2_MAP_YCBCR_ENC_DEFAULT(sensor->fmt.colorspace);
    sensor->fmt.quantization  = V4L2_QUANTIZATION_FULL_RANGE;
    sensor->fmt.xfer_func     = V4L2_MAP_XFER_FUNC_DEFAULT(sensor->fmt.colorspace);
    dev_dbg(dev, "PROBE: Default format initialized - %ux%u, code=0x%08x", sensor->fmt.width, sensor->fmt.height, sensor->fmt.code);

	/* Get reset GPIO */
	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset_gpio)) {
		ret = PTR_ERR(sensor->reset_gpio);
		dev_warn(dev, "Cannot get reset GPIO: %d\n", ret);
		sensor->reset_gpio = NULL;
	}
	dev_dbg(dev, "PROBE: Reset GPIO %s", sensor->reset_gpio ? "configured" : "not available");

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

    ret = of_property_read_u32(dev->of_node, "csi_id", &(sensor->csi_id));
    if (ret) {
        dev_err(dev, "csi id missing or invalid\n");
        return ret;
    }

	/* Initialize V4L2 subdev */
	v4l2_i2c_subdev_init(&sensor->sd, client, &flir_boson_subdev_ops);
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
    sensor->sd.entity.ops      = &flir_boson_media_ops;
    strscpy(sensor->sd.name, "flir_boson", sizeof(sensor->sd.name));
	dev_dbg(dev, "PROBE: V4L2 subdev initialized");

	/* Initialize media pad */
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret) {
		dev_err(dev, "Could not init media entity\n");
		goto cleanup_mutex;
	}
	dev_dbg(dev, "PROBE: Media pad initialized");

	/* Register V4L2 subdev */
	dev_dbg(dev, "PROBE: Registering V4L2 async subdev");
	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret) {
		dev_err(dev, "Could not register v4l2 device: %d\n", ret);
		goto cleanup_entity;
	}
	dev_dbg(dev, "PROBE: V4L2 subdev registered successfully");

	if (sensor->reset_gpio) {
		gpiod_set_value_cansleep(sensor->reset_gpio, 1);
		msleep(4);
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);

	    /* Wait for camera boot (2.5 seconds as per spec) */
		msleep(2700);
	}

	/* Get camera serial number */
	if (flir_boson_get_int_val(sensor, BOSON_GETCAMERASN, &sensor->camera_sn) == R_SUCCESS)
		dev_info(dev, "Camera SN: 0x%08X", sensor->camera_sn);
	else
        dev_warn(dev, "Could not read camera serial number");

	if (flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_OFF, 1) != R_SUCCESS)
        dev_warn(dev, "Could not set MIPI state to OFF");
	sensor->mipi_state = FLR_DVO_MIPI_STATE_OFF;

	dev_info(dev, "FLIR Boson+ MIPI camera driver loaded\n");
	dev_dbg(dev, "PROBE: Complete - device ready for operation");
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
    struct v4l2_subdev    *sd     = i2c_get_clientdata(client);
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

/* Hardware Mode: I2C Driver */
static struct i2c_driver flir_boson_i2c_driver = {
    .driver =
        {
            .name           = "flir_boson",
		.of_match_table = flir_boson_dt_ids,
	},
	.id_table = flir_boson_id,
    .probe    = flir_boson_probe,
    .remove   = flir_boson_remove,
};

static int __init flir_boson_driver_init(void) {

	pr_info("FLIR Boson+ Driver: Starting in HARDWARE MODE\n");
	return i2c_add_driver(&flir_boson_i2c_driver);
}
module_init(flir_boson_driver_init);

static void __exit flir_boson_driver_exit(void) { i2c_del_driver(&flir_boson_i2c_driver); }
module_exit(flir_boson_driver_exit);

MODULE_DESCRIPTION("FLIR Boson+ MIPI Camera V4L2 Driver");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FLIR Driver Development");
MODULE_VERSION("1.0");
