// SPDX-License-Identifier: GPL-2.0
/*
 * FLIR Boson+ VVCAM-compatible sensor bridge
 * Minimal RAW14 mono support for ISP pipeline
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include "ReturnCodes.h"
#include "flir-boson.h"
#include "vvsensor.h"

#define BOSON_DEFAULT_WIDTH 640
#define BOSON_DEFAULT_HEIGHT 512
#define BOSON_DEFAULT_FPS 60
#define BOSON_CHIP_ID 0x0B05

static const u64 boson_link_freqs[] = {400000000ULL};

static const struct vvcam_mode_info_s boson_modes[] = {
    {
        .index = 0,
        .size =
            {
                .bounds_width  = BOSON_DEFAULT_WIDTH,
                .bounds_height = BOSON_DEFAULT_HEIGHT,
                .top           = 0,
                .left          = 0,
                .width         = BOSON_DEFAULT_WIDTH,
                .height        = BOSON_DEFAULT_HEIGHT,
            },
        .hdr_mode  = SENSOR_MODE_LINEAR,
        .bit_width = 14,
        .data_compress =
            {
                .enable = 0,
            },
        .bayer_pattern = BAYER_GRBG,
        .ae_info =
            {
                .def_frm_len_lines     = BOSON_DEFAULT_HEIGHT,
                .curr_frm_len_lines    = BOSON_DEFAULT_HEIGHT - 1,
                .one_line_exp_time_ns  = 30000,
                .max_integration_line  = BOSON_DEFAULT_HEIGHT - 1,
                .min_integration_line  = 4,
                .max_again             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .min_again             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .max_dgain             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .min_dgain             = 1 * (1 << SENSOR_FIX_FRACBITS),
                .gain_step             = 1,
                .start_exposure        = 1000 * (1 << SENSOR_FIX_FRACBITS),
                .cur_fps               = BOSON_DEFAULT_FPS * (1 << SENSOR_FIX_FRACBITS),
                .max_fps               = BOSON_DEFAULT_FPS * (1 << SENSOR_FIX_FRACBITS),
                .min_fps               = 1 * (1 << SENSOR_FIX_FRACBITS),
                .min_afps              = 1 * (1 << SENSOR_FIX_FRACBITS),
                .int_update_delay_frm  = 1,
                .gain_update_delay_frm = 1,
            },
        .mipi_info =
            {
                .mipi_lane = 2,
            },
    },
};

static int boson_result_to_errno(FLR_RESULT result) {
    switch (result) {
    case R_SUCCESS: return 0;
    case FLR_BAD_ARG_POINTER_ERROR:
    case R_CAM_API_INVALID_INPUT: return -EINVAL;
    case FLR_COMM_TIMEOUT_ERROR:
    case FLR_COMM_ERROR_READING_COMM: return -EIO;
    case FLR_NOT_READY:
    case FLR_CAM_BUSY: return -EBUSY;
    case FLR_RANGE_ERROR:
    case FLR_DATA_SIZE_ERROR: return -ERANGE;
    case R_SDK_PKG_BUFFER_OVERFLOW:
    case R_CAM_PKG_BUFFER_OVERFLOW: return -ENOSPC;
    case FLR_COMM_PORT_NOT_OPEN:
    case FLR_COMM_NO_DEV: return -ENODEV;
    case R_SDK_DSPCH_SEQUENCE_MISMATCH:
    case R_SDK_DSPCH_ID_MISMATCH: return -EPROTO;
    default: return -EREMOTEIO;
    }
}

static void boson_fill_default_fmt(struct v4l2_mbus_framefmt *fmt) {
    fmt->code         = MEDIA_BUS_FMT_Y14_1X14;
    fmt->width        = BOSON_DEFAULT_WIDTH;
    fmt->height       = BOSON_DEFAULT_HEIGHT;
    fmt->field        = V4L2_FIELD_NONE;
    fmt->colorspace   = V4L2_COLORSPACE_RAW;
    fmt->ycbcr_enc    = V4L2_YCBCR_ENC_DEFAULT;
    fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
    fmt->xfer_func    = V4L2_XFER_FUNC_NONE;
}

static int boson_set_mipi_state(struct flir_boson_dev *sensor, u32 state) {
    FLR_RESULT ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, state, 5);

    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    sensor->mipi_state = state;
    return 0;
}

static int boson_configure_raw14(struct flir_boson_dev *sensor) {
    FLR_RESULT ret;

    ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPISTATE, FLR_DVO_MIPI_STATE_OFF, 5);
    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    ret = flir_boson_send_int_cmd(sensor, DVO_SETTYPE, FLR_DVO_TYPE_TLINEAR, 10);
    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    ret = flir_boson_send_int_cmd(sensor, DVO_SETOUTPUTFORMAT, FLR_DVO_IR16, 5);
    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    ret = flir_boson_send_int_cmd(sensor, DVO_SETOUTPUTIR16FORMAT, FLR_DVO_IR16_16B, 5);
    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    ret = flir_boson_set_dvo_muxtype(sensor, FLR_DVOMUX_OUTPUT_IF_MIPITX, FLR_DVOMUX_SRC_IR, FLR_DVOMUX_TYPE_MONO14);
    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    ret = flir_boson_send_int_cmd(sensor, DVO_SETOUTPUTINTERFACE, FLR_DVO_MIPI, 5);
    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    ret = flir_boson_send_int_cmd(sensor, DVO_SETMIPICLOCKLANEMODE, FLR_DVO_MIPI_CLOCK_LANE_MODE_CONTINUOUS, 5);
    if (ret != R_SUCCESS) return boson_result_to_errno(ret);

    sensor->mode_change = false;
    return 0;
}

static int boson_query_cap(struct flir_boson_dev *sensor, void *arg) {
    struct v4l2_capability *cap = arg;

    strscpy(cap->driver, "bosonplus", sizeof(cap->driver));
    strscpy(cap->card, "FLIR Boson+", sizeof(cap->card));
    snprintf(cap->bus_info, sizeof(cap->bus_info), "i2c-%d", sensor->i2c_client->adapter ? sensor->i2c_client->adapter->nr : 0);

    return 0;
}

static int boson_query_modes(void *arg) {
    struct vvcam_mode_info_array_s __user *user_modes = arg;
    u32                                    count      = ARRAY_SIZE(boson_modes);
    int                                    ret;

    ret = copy_to_user(&user_modes->count, &count, sizeof(count));
    ret |= copy_to_user(&user_modes->modes, boson_modes, sizeof(boson_modes));

    return ret ? -EFAULT : 0;
}

static int boson_get_sensor_mode(struct flir_boson_dev *sensor, void *arg) { return copy_to_user(arg, sensor->mode, sizeof(*sensor->mode)) ? -EFAULT : 0; }

static int boson_set_sensor_mode(struct flir_boson_dev *sensor, void *arg) {
    struct vvcam_mode_info_s mode;
    int                      ret;

    ret = copy_from_user(&mode, arg, sizeof(mode));
    if (ret) return -EFAULT;

    if (mode.index >= ARRAY_SIZE(boson_modes)) return -EINVAL;

    sensor->mode        = &boson_modes[mode.index];
    sensor->mode_change = true;
    return 0;
}

static int boson_get_clk(void *arg) {
    struct vvcam_clk_s clk = {
        .sensor_mclk       = 24000000,
        .csi_max_pixel_clk = 24000000 * 8,
    };

    return copy_to_user(arg, &clk, sizeof(clk)) ? -EFAULT : 0;
}

static int boson_set_fps(struct flir_boson_dev *sensor, void *arg) {
    u32 fps;

    if (copy_from_user(&fps, arg, sizeof(fps))) return -EFAULT;

    /* Boson clocking is fixed; accept and ignore. */
    return 0;
}

static int boson_get_fps(void *arg) {
    u32 fps = BOSON_DEFAULT_FPS;

    return copy_to_user(arg, &fps, sizeof(fps)) ? -EFAULT : 0;
}

static int boson_stream_ctl(struct flir_boson_dev *sensor, bool on) {
    int ret = 0;

    if (on) {
        if (sensor->mode_change) {
            ret = boson_configure_raw14(sensor);
            if (ret) return ret;
        }
        ret = boson_set_mipi_state(sensor, FLR_DVO_MIPI_STATE_ACTIVE);
        if (!ret) sensor->streaming = true;
    } else {
        ret = boson_set_mipi_state(sensor, FLR_DVO_MIPI_STATE_OFF);
        if (!ret) sensor->streaming = false;
    }

    return ret;
}

static long boson_priv_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg) {
    struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
    long                   ret    = 0;
    u32                    value;

    mutex_lock(&sensor->lock);

    switch (cmd) {
    case VVSENSORIOC_S_POWER:
    case VVSENSORIOC_S_CLK:
    case VVSENSORIOC_RESET: ret = 0; break;
    case VVSENSORIOC_G_CLK: ret = boson_get_clk(arg); break;
    case VIDIOC_QUERYCAP: ret = boson_query_cap(sensor, arg); break;
    case VVSENSORIOC_QUERY: ret = boson_query_modes(arg); break;
    case VVSENSORIOC_G_CHIP_ID:
        value = BOSON_CHIP_ID;
        ret   = copy_to_user(arg, &value, sizeof(value)) ? -EFAULT : 0;
        break;
    case VVSENSORIOC_G_SENSOR_MODE: ret = boson_get_sensor_mode(sensor, arg); break;
    case VVSENSORIOC_S_SENSOR_MODE: ret = boson_set_sensor_mode(sensor, arg); break;
    case VVSENSORIOC_S_STREAM:
        ret = copy_from_user(&value, arg, sizeof(value));
        if (ret) break;
        ret = boson_stream_ctl(sensor, !!value);
        break;
    case VVSENSORIOC_S_EXP:
    case VVSENSORIOC_S_GAIN: ret = copy_from_user(&value, arg, sizeof(value)) ? -EFAULT : 0; break;
    case VVSENSORIOC_S_FPS: ret = boson_set_fps(sensor, arg); break;
    case VVSENSORIOC_G_FPS: ret = boson_get_fps(arg); break;
    default: ret = -ENOIOCTLCMD; break;
    }

    mutex_unlock(&sensor->lock);
    return ret;
}

static int boson_s_power(struct v4l2_subdev *sd, int on) {
    struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
    int                    ret    = 0;

    mutex_lock(&sensor->lock);

    if (on && !sensor->powered) {
        sensor->powered     = true;
        sensor->mode_change = true;
    } else if (!on && sensor->powered) {
        ret               = boson_set_mipi_state(sensor, FLR_DVO_MIPI_STATE_OFF);
        sensor->powered   = false;
        sensor->streaming = false;
    }

    mutex_unlock(&sensor->lock);
    return ret;
}

static int boson_s_stream(struct v4l2_subdev *sd, int enable) {
    struct flir_boson_dev *sensor = to_flir_boson_dev(sd);
    int                    ret;

    mutex_lock(&sensor->lock);
    ret = boson_stream_ctl(sensor, enable);
    mutex_unlock(&sensor->lock);

    return ret;
}

static int boson_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_mbus_code_enum *code) {
    if (code->pad || code->index) return -EINVAL;

    code->code = MEDIA_BUS_FMT_Y14_1X14;
    return 0;
}

static int boson_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_format *fmt) {
    struct flir_boson_dev *sensor = to_flir_boson_dev(sd);

    if (fmt->pad) return -EINVAL;

    mutex_lock(&sensor->lock);
    fmt->format = sensor->fmt;
    mutex_unlock(&sensor->lock);

    return 0;
}

static int boson_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_state *sd_state, struct v4l2_subdev_format *fmt) {
    struct flir_boson_dev *sensor = to_flir_boson_dev(sd);

    if (fmt->pad) return -EINVAL;

    boson_fill_default_fmt(&fmt->format);

    mutex_lock(&sensor->lock);
    sensor->fmt         = fmt->format;
    sensor->mode        = &boson_modes[0];
    sensor->mode_change = true;
    mutex_unlock(&sensor->lock);

    return 0;
}

static const struct v4l2_subdev_core_ops boson_core_ops = {
    .s_power = boson_s_power,
    .ioctl   = boson_priv_ioctl,
};

static const struct v4l2_subdev_video_ops boson_video_ops = {
    .s_stream = boson_s_stream,
};

static const struct v4l2_subdev_pad_ops boson_pad_ops = {
    .enum_mbus_code = boson_enum_mbus_code,
    .get_fmt        = boson_get_fmt,
    .set_fmt        = boson_set_fmt,
};

static const struct v4l2_subdev_ops boson_subdev_ops = {
    .core  = &boson_core_ops,
    .video = &boson_video_ops,
    .pad   = &boson_pad_ops,
};

static int boson_link_setup(struct media_entity *entity, const struct media_pad *local, const struct media_pad *remote, u32 flags) { return 0; }

static const struct media_entity_operations boson_media_ops = {
    .link_setup = boson_link_setup,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int flir_boson_probe(struct i2c_client *client)
#else
static int flir_boson_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
    struct flir_boson_dev *sensor;
    int                    ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
    (void)id;
#endif

    sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
    if (!sensor) return -ENOMEM;

    sensor->dev        = &client->dev;
    sensor->i2c_client = client;
    mutex_init(&sensor->lock);

    sensor->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(sensor->reset_gpio)) return PTR_ERR(sensor->reset_gpio);

    if (sensor->reset_gpio) {
        gpiod_set_value_cansleep(sensor->reset_gpio, 0);
        msleep(5);
        gpiod_set_value_cansleep(sensor->reset_gpio, 1);
        msleep(50);
    }

    boson_fill_default_fmt(&sensor->fmt);
    sensor->mode        = &boson_modes[0];
    sensor->mode_change = true;
    sensor->pixel_rate  = (u64)BOSON_DEFAULT_WIDTH * BOSON_DEFAULT_HEIGHT * BOSON_DEFAULT_FPS;
    sensor->link_freq   = boson_link_freqs[0];

    sensor->ep.bus_type                     = V4L2_MBUS_CSI2_DPHY;
    sensor->ep.bus.mipi_csi2.num_data_lanes = 2;

    v4l2_i2c_subdev_init(&sensor->sd, client, &boson_subdev_ops);
    sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
    sensor->sd.entity.ops      = &boson_media_ops;
    sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

    sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
    ret               = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
    if (ret) return ret;

    i2c_set_clientdata(client, sensor);
    ret = v4l2_async_register_subdev(&sensor->sd);
    if (ret) {
        media_entity_cleanup(&sensor->sd.entity);
        return ret;
    }

    return 0;
}

static void flir_boson_remove(struct i2c_client *client) {
    struct flir_boson_dev *sensor = i2c_get_clientdata(client);

    v4l2_async_unregister_subdev(&sensor->sd);
    media_entity_cleanup(&sensor->sd.entity);
}

static const struct of_device_id flir_boson_of_match[] = {{.compatible = "flir,boson-plus"}, {/* sentinel */}};
MODULE_DEVICE_TABLE(of, flir_boson_of_match);

static const struct i2c_device_id flir_boson_i2c_id[] = {{"flir-boson-plus", 0}, {}};
MODULE_DEVICE_TABLE(i2c, flir_boson_i2c_id);

static struct i2c_driver flir_boson_driver = {
    .driver =
        {
            .name           = "flir-boson-plus",
            .of_match_table = flir_boson_of_match,
        },
    .probe    = flir_boson_probe,
    .remove   = flir_boson_remove,
    .id_table = flir_boson_i2c_id,
};

module_i2c_driver(flir_boson_driver);

MODULE_DESCRIPTION("FLIR Boson+ RAW14 VVCAM sensor driver");
MODULE_LICENSE("GPL");
