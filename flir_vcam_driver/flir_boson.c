/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************/

#include "isi.h"
#include "isi_iss.h"
#include "isi_priv.h"
#include "vvsensor.h"
#include <common/misc.h>
#include <common/return_codes.h>
#include <ebase/builtins.h>
#include <ebase/trace.h>
#include <ebase/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>

CREATE_TRACER(FLIR_BOSON_INFO, "FLIR_BOSON: ", INFO, 0);
CREATE_TRACER(FLIR_BOSON_WARN, "FLIR_BOSON: ", WARNING, 0);
CREATE_TRACER(FLIR_BOSON_ERROR, "FLIR_BOSON: ", ERROR, 1);

#ifdef SUBDEV_V4L2
#include <fcntl.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif

static const char SensorName[16] = "flir_boson";

typedef struct FLIR_BOSON_Context_s {
    IsiSensorContext_t       IsiCtx;
    struct vvcam_mode_info_s CurMode;
    IsiSensorAeInfo_t        AeInfo;
    IsiSensorIntTime_t       IntTime;
    uint32_t                 LongIntLine;
    uint32_t                 IntLine;
    uint32_t                 ShortIntLine;
    IsiSensorGain_t          SensorGain;
    uint32_t                 minAfps;
    uint64_t                 AEStartExposure;
    int                      motor_fd;
    uint32_t                 focus_mode;
} FLIR_BOSON_Context_t;

static inline int OpenMotorDevice(const vvcam_lens_t *pfocus_lens) {
    int                    filep;
    char                   szFile[32];
    struct v4l2_capability caps;
    for (int i = 0; i < 20; i++) {
        sprintf(szFile, "/dev/v4l-subdev%d", i);
        filep = open(szFile, O_RDWR | O_NONBLOCK);
        if (filep < 0) { continue; }

        if (ioctl(filep, VIDIOC_QUERYCAP, &caps) < 0) {
            close(filep);
            continue;
        }

        if (strcmp((char *)caps.driver, (char *)pfocus_lens->name) || (atoi((char *)caps.bus_info) != pfocus_lens->id)) {
            close(filep);
            continue;
        } else {
            return filep;
        }
    }
    return -1;
}

static RESULT FLIR_BOSON_IsiSensorSetPowerIss(IsiSensorHandle_t handle, bool_t on) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);
    TRACE(FLIR_BOSON_INFO, "%s: set power %d\n", __func__, on);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    int32_t power = on;
    ret           = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_POWER, &power);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s set power %d error\n", __func__, power);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSensorGetClkIss(IsiSensorHandle_t handle, struct vvcam_clk_s *pclk) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (!pclk) return RET_NULL_POINTER;

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_G_CLK, pclk);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s get clock error\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: status:%d sensor_mclk:%d csi_max_pixel_clk:%d\n", __func__, pclk->status, pclk->sensor_mclk, pclk->csi_max_pixel_clk);
    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSensorSetClkIss(IsiSensorHandle_t handle, struct vvcam_clk_s *pclk) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (pclk == NULL) return RET_NULL_POINTER;

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_CLK, &pclk);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s set clk error\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: status:%d sensor_mclk:%d csi_max_pixel_clk:%d\n", __func__, pclk->status, pclk->sensor_mclk, pclk->csi_max_pixel_clk);

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiResetSensorIss(IsiSensorHandle_t handle) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_RESET, NULL);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s set reset error\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiRegisterReadIss(IsiSensorHandle_t handle, const uint32_t address, uint32_t *pValue) {
    int32_t ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    struct vvcam_sccb_data_s sccb_data;
    sccb_data.addr = address;
    sccb_data.data = 0;
    ret            = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_READ_REG, &sccb_data);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: read sensor register error!\n", __func__);
        return (RET_FAILURE);
    }

    *pValue = sccb_data.data;

    TRACE(FLIR_BOSON_INFO, "%s (exit) \n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiRegisterWriteIss(IsiSensorHandle_t handle, const uint32_t address, const uint32_t value) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    struct vvcam_sccb_data_s sccb_data;
    sccb_data.addr = address;
    sccb_data.data = value;

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_WRITE_REG, &sccb_data);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: write sensor register error!\n", __func__);
        return (RET_FAILURE);
    }

    TRACE(FLIR_BOSON_INFO, "%s (exit) \n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_UpdateIsiAEInfo(IsiSensorHandle_t handle) {
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    uint32_t exp_line_time = pFLIR_BOSONCtx->CurMode.ae_info.one_line_exp_time_ns;

    IsiSensorAeInfo_t *pAeInfo = &pFLIR_BOSONCtx->AeInfo;
    pAeInfo->oneLineExpTime    = (exp_line_time << ISI_EXPO_PARAS_FIX_FRACBITS) / 1000;

    if (pFLIR_BOSONCtx->CurMode.hdr_mode == SENSOR_MODE_LINEAR) {
        pAeInfo->maxIntTime.linearInt     = pFLIR_BOSONCtx->CurMode.ae_info.max_integration_line * pAeInfo->oneLineExpTime;
        pAeInfo->minIntTime.linearInt     = pFLIR_BOSONCtx->CurMode.ae_info.min_integration_line * pAeInfo->oneLineExpTime;
        pAeInfo->maxAGain.linearGainParas = pFLIR_BOSONCtx->CurMode.ae_info.max_again;
        pAeInfo->minAGain.linearGainParas = pFLIR_BOSONCtx->CurMode.ae_info.min_again;
        pAeInfo->maxDGain.linearGainParas = pFLIR_BOSONCtx->CurMode.ae_info.max_dgain;
        pAeInfo->minDGain.linearGainParas = pFLIR_BOSONCtx->CurMode.ae_info.min_dgain;
    } else {
        switch (pFLIR_BOSONCtx->CurMode.stitching_mode) {
        case SENSOR_STITCHING_DUAL_DCG:
        case SENSOR_STITCHING_3DOL:
        case SENSOR_STITCHING_LINEBYLINE:
            pAeInfo->maxIntTime.triInt.triSIntTime = pFLIR_BOSONCtx->CurMode.ae_info.max_vsintegration_line * pAeInfo->oneLineExpTime;
            pAeInfo->minIntTime.triInt.triSIntTime = pFLIR_BOSONCtx->CurMode.ae_info.min_vsintegration_line * pAeInfo->oneLineExpTime;

            pAeInfo->maxIntTime.triInt.triIntTime = pFLIR_BOSONCtx->CurMode.ae_info.max_integration_line * pAeInfo->oneLineExpTime;
            pAeInfo->minIntTime.triInt.triIntTime = pFLIR_BOSONCtx->CurMode.ae_info.min_integration_line * pAeInfo->oneLineExpTime;

            if (pFLIR_BOSONCtx->CurMode.stitching_mode == SENSOR_STITCHING_DUAL_DCG) {
                pAeInfo->maxIntTime.triInt.triLIntTime = pAeInfo->maxIntTime.triInt.triIntTime;
                pAeInfo->minIntTime.triInt.triLIntTime = pAeInfo->minIntTime.triInt.triIntTime;
            } else {
                pAeInfo->maxIntTime.triInt.triLIntTime = pFLIR_BOSONCtx->CurMode.ae_info.max_longintegration_line * pAeInfo->oneLineExpTime;
                pAeInfo->minIntTime.triInt.triLIntTime = pFLIR_BOSONCtx->CurMode.ae_info.min_longintegration_line * pAeInfo->oneLineExpTime;
            }

            pAeInfo->maxAGain.triGainParas.triSGain = pFLIR_BOSONCtx->CurMode.ae_info.max_short_again;
            pAeInfo->minAGain.triGainParas.triSGain = pFLIR_BOSONCtx->CurMode.ae_info.min_short_again;
            pAeInfo->maxDGain.triGainParas.triSGain = pFLIR_BOSONCtx->CurMode.ae_info.max_short_dgain;
            pAeInfo->minDGain.triGainParas.triSGain = pFLIR_BOSONCtx->CurMode.ae_info.min_short_dgain;

            pAeInfo->maxAGain.triGainParas.triGain = pFLIR_BOSONCtx->CurMode.ae_info.max_again;
            pAeInfo->minAGain.triGainParas.triGain = pFLIR_BOSONCtx->CurMode.ae_info.min_again;
            pAeInfo->maxDGain.triGainParas.triGain = pFLIR_BOSONCtx->CurMode.ae_info.max_dgain;
            pAeInfo->minDGain.triGainParas.triGain = pFLIR_BOSONCtx->CurMode.ae_info.min_dgain;

            pAeInfo->maxAGain.triGainParas.triLGain = pFLIR_BOSONCtx->CurMode.ae_info.max_long_again;
            pAeInfo->minAGain.triGainParas.triLGain = pFLIR_BOSONCtx->CurMode.ae_info.min_long_again;
            pAeInfo->maxDGain.triGainParas.triLGain = pFLIR_BOSONCtx->CurMode.ae_info.max_long_dgain;
            pAeInfo->minDGain.triGainParas.triLGain = pFLIR_BOSONCtx->CurMode.ae_info.min_long_dgain;
            break;
        case SENSOR_STITCHING_DUAL_DCG_NOWAIT:
        case SENSOR_STITCHING_16BIT_COMPRESS:
        case SENSOR_STITCHING_L_AND_S:
        case SENSOR_STITCHING_2DOL:
            pAeInfo->maxIntTime.dualInt.dualIntTime = pFLIR_BOSONCtx->CurMode.ae_info.max_integration_line * pAeInfo->oneLineExpTime;
            pAeInfo->minIntTime.dualInt.dualIntTime = pFLIR_BOSONCtx->CurMode.ae_info.min_integration_line * pAeInfo->oneLineExpTime;

            if (pFLIR_BOSONCtx->CurMode.stitching_mode == SENSOR_STITCHING_DUAL_DCG_NOWAIT) {
                pAeInfo->maxIntTime.dualInt.dualSIntTime = pAeInfo->maxIntTime.dualInt.dualIntTime;
                pAeInfo->minIntTime.dualInt.dualSIntTime = pAeInfo->minIntTime.dualInt.dualIntTime;
            } else {
                pAeInfo->maxIntTime.dualInt.dualSIntTime = pFLIR_BOSONCtx->CurMode.ae_info.max_vsintegration_line * pAeInfo->oneLineExpTime;
                pAeInfo->minIntTime.dualInt.dualSIntTime = pFLIR_BOSONCtx->CurMode.ae_info.min_vsintegration_line * pAeInfo->oneLineExpTime;
            }

            if (pFLIR_BOSONCtx->CurMode.stitching_mode == SENSOR_STITCHING_DUAL_DCG_NOWAIT) {
                pAeInfo->maxAGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.max_again;
                pAeInfo->minAGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.min_again;
                pAeInfo->maxDGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.max_dgain;
                pAeInfo->minDGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.min_dgain;
                pAeInfo->maxAGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.max_long_again;
                pAeInfo->minAGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.min_long_again;
                pAeInfo->maxDGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.max_long_dgain;
                pAeInfo->minDGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.min_long_dgain;
            } else {
                pAeInfo->maxAGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.max_short_again;
                pAeInfo->minAGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.min_short_again;
                pAeInfo->maxDGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.max_short_dgain;
                pAeInfo->minDGain.dualGainParas.dualSGain = pFLIR_BOSONCtx->CurMode.ae_info.min_short_dgain;
                pAeInfo->maxAGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.max_again;
                pAeInfo->minAGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.min_again;
                pAeInfo->maxDGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.max_dgain;
                pAeInfo->minDGain.dualGainParas.dualGain  = pFLIR_BOSONCtx->CurMode.ae_info.min_dgain;
            }

            break;
        default: break;
        }
    }
    pAeInfo->gainStep    = pFLIR_BOSONCtx->CurMode.ae_info.gain_step;
    pAeInfo->currFps     = pFLIR_BOSONCtx->CurMode.ae_info.cur_fps;
    pAeInfo->maxFps      = pFLIR_BOSONCtx->CurMode.ae_info.max_fps;
    pAeInfo->minFps      = pFLIR_BOSONCtx->CurMode.ae_info.min_fps;
    pAeInfo->minAfps     = pFLIR_BOSONCtx->CurMode.ae_info.min_afps;
    pAeInfo->hdrRatio[0] = pFLIR_BOSONCtx->CurMode.ae_info.hdr_ratio.ratio_l_s;
    pAeInfo->hdrRatio[1] = pFLIR_BOSONCtx->CurMode.ae_info.hdr_ratio.ratio_s_vs;

    pAeInfo->intUpdateDlyFrm  = pFLIR_BOSONCtx->CurMode.ae_info.int_update_delay_frm;
    pAeInfo->gainUpdateDlyFrm = pFLIR_BOSONCtx->CurMode.ae_info.gain_update_delay_frm;

    if (pFLIR_BOSONCtx->minAfps != 0) { pAeInfo->minAfps = pFLIR_BOSONCtx->minAfps; }
    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetSensorModeIss(IsiSensorHandle_t handle, IsiSensorMode_t *pMode) {
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    if (pMode == NULL) return (RET_NULL_POINTER);

    memcpy(pMode, &pFLIR_BOSONCtx->CurMode, sizeof(IsiSensorMode_t));

    TRACE(FLIR_BOSON_INFO, "%s (exit) \n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSetSensorModeIss(IsiSensorHandle_t handle, IsiSensorMode_t *pMode) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (pMode == NULL) return (RET_NULL_POINTER);

    struct vvcam_mode_info_s sensor_mode;
    memset(&sensor_mode, 0, sizeof(struct vvcam_mode_info_s));
    sensor_mode.index = pMode->index;

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_SENSOR_MODE, &sensor_mode);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s set sensor mode error\n", __func__);
        return RET_FAILURE;
    }

    memset(&sensor_mode, 0, sizeof(struct vvcam_mode_info_s));
    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_G_SENSOR_MODE, &sensor_mode);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s set sensor mode failed", __func__);
        return RET_FAILURE;
    }
    memcpy(&pFLIR_BOSONCtx->CurMode, &sensor_mode, sizeof(struct vvcam_mode_info_s));
    FLIR_BOSON_UpdateIsiAEInfo(handle);

    TRACE(FLIR_BOSON_INFO, "%s (exit) \n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSensorSetStreamingIss(IsiSensorHandle_t handle, bool_t on) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    uint32_t status = on;
    ret             = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_STREAM, &status);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s set sensor stream %d error\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: set streaming %d\n", __func__, on);
    TRACE(FLIR_BOSON_INFO, "%s (exit) \n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiCreateSensorIss(IsiSensorInstanceConfig_t *pConfig) {
    RESULT                result = RET_SUCCESS;
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    if (!pConfig || !pConfig->pSensor || !pConfig->HalHandle) return RET_NULL_POINTER;

    pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)malloc(sizeof(FLIR_BOSON_Context_t));
    if (!pFLIR_BOSONCtx) return RET_OUTOFMEM;

    memset(pFLIR_BOSONCtx, 0, sizeof(FLIR_BOSON_Context_t));
    pFLIR_BOSONCtx->IsiCtx.HalHandle = pConfig->HalHandle;
    pFLIR_BOSONCtx->IsiCtx.pSensor   = pConfig->pSensor;
    pConfig->hSensor                 = (IsiSensorHandle_t)pFLIR_BOSONCtx;

    result = FLIR_BOSON_IsiSensorSetPowerIss(pFLIR_BOSONCtx, BOOL_TRUE);
    if (result != RET_SUCCESS) {
        TRACE(FLIR_BOSON_ERROR, "%s set power error\n", __func__);
        return RET_FAILURE;
    }
    struct vvcam_clk_s clk;
    memset(&clk, 0, sizeof(struct vvcam_clk_s));
    result = FLIR_BOSON_IsiSensorGetClkIss(pFLIR_BOSONCtx, &clk);
    if (result != RET_SUCCESS) {
        TRACE(FLIR_BOSON_ERROR, "%s get clk error\n", __func__);
        return RET_FAILURE;
    }
    clk.status = 1;
    result     = FLIR_BOSON_IsiSensorSetClkIss(pFLIR_BOSONCtx, &clk);
    if (result != RET_SUCCESS) {
        TRACE(FLIR_BOSON_ERROR, "%s set clk error\n", __func__);
        return RET_FAILURE;
    }
    result = FLIR_BOSON_IsiResetSensorIss(pFLIR_BOSONCtx);
    if (result != RET_SUCCESS) {
        TRACE(FLIR_BOSON_ERROR, "%s retset sensor error\n", __func__);
        return RET_FAILURE;
    }

    IsiSensorMode_t SensorMode;
    SensorMode.index = pConfig->SensorModeIndex;
    result           = FLIR_BOSON_IsiSetSensorModeIss(pFLIR_BOSONCtx, &SensorMode);
    if (result != RET_SUCCESS) {
        TRACE(FLIR_BOSON_ERROR, "%s set sensor mode error\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return result;
}

static RESULT FLIR_BOSON_IsiReleaseSensorIss(IsiSensorHandle_t handle) {
    TRACE(FLIR_BOSON_INFO, "%s (enter) \n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    if (pFLIR_BOSONCtx == NULL) return (RET_WRONG_HANDLE);

    FLIR_BOSON_IsiSensorSetStreamingIss(pFLIR_BOSONCtx, BOOL_FALSE);
    struct vvcam_clk_s clk;
    memset(&clk, 0, sizeof(struct vvcam_clk_s));
    FLIR_BOSON_IsiSensorGetClkIss(pFLIR_BOSONCtx, &clk);
    clk.status = 0;
    FLIR_BOSON_IsiSensorSetClkIss(pFLIR_BOSONCtx, &clk);
    FLIR_BOSON_IsiSensorSetPowerIss(pFLIR_BOSONCtx, BOOL_FALSE);
    free(pFLIR_BOSONCtx);
    pFLIR_BOSONCtx = NULL;

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiHalQuerySensorIss(HalHandle_t HalHandle, IsiSensorModeInfoArray_t *pSensorMode) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s (enter) \n", __func__);

    if (HalHandle == NULL || pSensorMode == NULL) return RET_NULL_POINTER;

    HalContext_t *pHalCtx = (HalContext_t *)HalHandle;
    ret                   = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_QUERY, pSensorMode);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: query sensor mode info error!\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiQuerySensorIss(IsiSensorHandle_t handle, IsiSensorModeInfoArray_t *pSensorMode) {
    RESULT result = RET_SUCCESS;

    TRACE(FLIR_BOSON_INFO, "%s (enter) \n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    result = FLIR_BOSON_IsiHalQuerySensorIss(pFLIR_BOSONCtx->IsiCtx.HalHandle, pSensorMode);
    if (result != RET_SUCCESS) TRACE(FLIR_BOSON_ERROR, "%s: query sensor mode info error!\n", __func__);

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return result;
}

static RESULT FLIR_BOSON_IsiGetCapsIss(IsiSensorHandle_t handle, IsiSensorCaps_t *pIsiSensorCaps) {
    RESULT result = RET_SUCCESS;

    TRACE(FLIR_BOSON_INFO, "%s (enter) \n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    if (pIsiSensorCaps == NULL) return RET_NULL_POINTER;

    IsiSensorModeInfoArray_t SensorModeInfo;
    memset(&SensorModeInfo, 0, sizeof(IsiSensorModeInfoArray_t));
    result = FLIR_BOSON_IsiQuerySensorIss(handle, &SensorModeInfo);
    if (result != RET_SUCCESS) {
        TRACE(FLIR_BOSON_ERROR, "%s: query sensor mode info error!\n", __func__);
        return RET_FAILURE;
    }

    pIsiSensorCaps->FieldSelection = ISI_FIELDSEL_BOTH;
    pIsiSensorCaps->YCSequence     = ISI_YCSEQ_YCBYCR;
    pIsiSensorCaps->Conv422        = ISI_CONV422_NOCOSITED;
    pIsiSensorCaps->HPol           = ISI_HPOL_REFPOS;
    pIsiSensorCaps->VPol           = ISI_VPOL_NEG;
    pIsiSensorCaps->Edge           = ISI_EDGE_RISING;
    pIsiSensorCaps->supportModeNum = SensorModeInfo.count;
    pIsiSensorCaps->currentMode    = pFLIR_BOSONCtx->CurMode.index;

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return result;
}

static RESULT FLIR_BOSON_IsiSetupSensorIss(IsiSensorHandle_t handle, const IsiSensorCaps_t *pIsiSensorCaps) {
    int    ret    = 0;
    RESULT result = RET_SUCCESS;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (pIsiSensorCaps == NULL) return RET_NULL_POINTER;

    if (pIsiSensorCaps->currentMode != pFLIR_BOSONCtx->CurMode.index) {
        IsiSensorMode_t SensorMode;
        memset(&SensorMode, 0, sizeof(IsiSensorMode_t));
        SensorMode.index = pIsiSensorCaps->currentMode;
        result           = FLIR_BOSON_IsiSetSensorModeIss(handle, &SensorMode);
        if (result != RET_SUCCESS) {
            TRACE(FLIR_BOSON_ERROR, "%s:set sensor mode %d failed!\n", __func__, SensorMode.index);
            return result;
        }
    }

#ifdef SUBDEV_V4L2
    struct v4l2_subdev_format format;
    memset(&format, 0, sizeof(struct v4l2_subdev_format));
    format.format.width  = pFLIR_BOSONCtx->CurMode.size.bounds_width;
    format.format.height = pFLIR_BOSONCtx->CurMode.size.bounds_height;
    format.which         = V4L2_SUBDEV_FORMAT_ACTIVE;
    format.pad           = 0;
    ret                  = ioctl(pHalCtx->sensor_fd, VIDIOC_SUBDEV_S_FMT, &format);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: sensor set format error!\n", __func__);
        return RET_FAILURE;
    }
#else
    ret = ioctrl(pHalCtx->sensor_fd, VVSENSORIOC_S_INIT, NULL);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: sensor init error!\n", __func__);
        return RET_FAILURE;
    }
#endif

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetSensorRevisionIss(IsiSensorHandle_t handle, uint32_t *pValue) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (pValue == NULL) return RET_NULL_POINTER;

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_G_CHIP_ID, pValue);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: get chip id error!\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiCheckSensorConnectionIss(IsiSensorHandle_t handle) {
    RESULT result = RET_SUCCESS;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    uint32_t ChipId = 0;
    result          = FLIR_BOSON_IsiGetSensorRevisionIss(handle, &ChipId);
    if (result != RET_SUCCESS) {
        TRACE(FLIR_BOSON_ERROR, "%s:get sensor chip id error!\n", __func__);
        return RET_FAILURE;
    }

    if (ChipId != 0x356) {
        TRACE(FLIR_BOSON_ERROR, "%s:ChipID=0x356,while read sensor Id=0x%x error!\n", __func__, ChipId);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetAeInfoIss(IsiSensorHandle_t handle, IsiSensorAeInfo_t *pAeInfo) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    if (pAeInfo == NULL) return RET_NULL_POINTER;

    memcpy(pAeInfo, &pFLIR_BOSONCtx->AeInfo, sizeof(IsiSensorAeInfo_t));

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetIntegrationTimeIss(IsiSensorHandle_t handle, IsiSensorIntTime_t *pIntegrationTime) {
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    memcpy(pIntegrationTime, &pFLIR_BOSONCtx->IntTime, sizeof(IsiSensorIntTime_t));

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSetIntegrationTimeIss(IsiSensorHandle_t handle, IsiSensorIntTime_t *pIntegrationTime) {
    int      ret = 0;
    uint32_t LongIntLine;
    uint32_t IntLine;
    uint32_t ShortIntLine;
    uint32_t oneLineTime;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (pIntegrationTime == NULL) return RET_NULL_POINTER;

    oneLineTime                         = pFLIR_BOSONCtx->AeInfo.oneLineExpTime;
    pFLIR_BOSONCtx->IntTime.expoFrmType = pIntegrationTime->expoFrmType;

    switch (pIntegrationTime->expoFrmType) {
    case ISI_EXPO_FRAME_TYPE_1FRAME:
        IntLine = (pIntegrationTime->IntegrationTime.linearInt + (oneLineTime / 2)) / oneLineTime;
        if (IntLine != pFLIR_BOSONCtx->IntLine) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_EXP, &IntLine);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor linear exp error!\n", __func__);
                return RET_FAILURE;
            }
            pFLIR_BOSONCtx->IntLine = IntLine;
        }
        TRACE(FLIR_BOSON_INFO, "%s set linear exp %d \n", __func__, IntLine);
        pFLIR_BOSONCtx->IntTime.IntegrationTime.linearInt = IntLine * oneLineTime;
        break;
    case ISI_EXPO_FRAME_TYPE_2FRAMES:
        IntLine = (pIntegrationTime->IntegrationTime.dualInt.dualIntTime + (oneLineTime / 2)) / oneLineTime;
        if (IntLine != pFLIR_BOSONCtx->IntLine) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_EXP, &IntLine);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor dual exp error!\n", __func__);
                return RET_FAILURE;
            }
            pFLIR_BOSONCtx->IntLine = IntLine;
        }

        if (pFLIR_BOSONCtx->CurMode.stitching_mode != SENSOR_STITCHING_DUAL_DCG_NOWAIT) {
            ShortIntLine = (pIntegrationTime->IntegrationTime.dualInt.dualSIntTime + (oneLineTime / 2)) / oneLineTime;
            if (ShortIntLine != pFLIR_BOSONCtx->ShortIntLine) {
                ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_VSEXP, &ShortIntLine);
                if (ret != 0) {
                    TRACE(FLIR_BOSON_ERROR, "%s:set sensor dual vsexp error!\n", __func__);
                    return RET_FAILURE;
                }
                pFLIR_BOSONCtx->ShortIntLine = ShortIntLine;
            }
        } else {
            ShortIntLine                 = IntLine;
            pFLIR_BOSONCtx->ShortIntLine = ShortIntLine;
        }
        TRACE(FLIR_BOSON_INFO, "%s set dual exp %d short_exp %d\n", __func__, IntLine, ShortIntLine);
        pFLIR_BOSONCtx->IntTime.IntegrationTime.dualInt.dualIntTime  = IntLine * oneLineTime;
        pFLIR_BOSONCtx->IntTime.IntegrationTime.dualInt.dualSIntTime = ShortIntLine * oneLineTime;
        break;
    case ISI_EXPO_FRAME_TYPE_3FRAMES:
        if (pFLIR_BOSONCtx->CurMode.stitching_mode != SENSOR_STITCHING_DUAL_DCG_NOWAIT) {
            LongIntLine = (pIntegrationTime->IntegrationTime.triInt.triLIntTime + (oneLineTime / 2)) / oneLineTime;
            if (LongIntLine != pFLIR_BOSONCtx->LongIntLine) {
                ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_LONG_EXP, &LongIntLine);
                if (ret != 0) {
                    TRACE(FLIR_BOSON_ERROR, "%s:set sensor tri lexp error!\n", __func__);
                    return RET_FAILURE;
                }
                pFLIR_BOSONCtx->LongIntLine = LongIntLine;
            }
        } else {
            LongIntLine                 = (pIntegrationTime->IntegrationTime.triInt.triIntTime + (oneLineTime / 2)) / oneLineTime;
            pFLIR_BOSONCtx->LongIntLine = LongIntLine;
        }

        IntLine = (pIntegrationTime->IntegrationTime.triInt.triIntTime + (oneLineTime / 2)) / oneLineTime;
        if (IntLine != pFLIR_BOSONCtx->IntLine) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_EXP, &IntLine);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor tri exp error!\n", __func__);
                return RET_FAILURE;
            }
            pFLIR_BOSONCtx->IntLine = IntLine;
        }

        ShortIntLine = (pIntegrationTime->IntegrationTime.triInt.triSIntTime + (oneLineTime / 2)) / oneLineTime;
        if (ShortIntLine != pFLIR_BOSONCtx->ShortIntLine) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_VSEXP, &ShortIntLine);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor tri vsexp error!\n", __func__);
                return RET_FAILURE;
            }
            pFLIR_BOSONCtx->ShortIntLine = ShortIntLine;
        }
        TRACE(FLIR_BOSON_INFO, "%s set tri long exp %d exp %d short_exp %d\n", __func__, LongIntLine, IntLine, ShortIntLine);
        pFLIR_BOSONCtx->IntTime.IntegrationTime.triInt.triLIntTime = LongIntLine * oneLineTime;
        pFLIR_BOSONCtx->IntTime.IntegrationTime.triInt.triIntTime  = IntLine * oneLineTime;
        pFLIR_BOSONCtx->IntTime.IntegrationTime.triInt.triSIntTime = ShortIntLine * oneLineTime;
        break;
    default: return RET_FAILURE; break;
    }

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetGainIss(IsiSensorHandle_t handle, IsiSensorGain_t *pGain) {
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    if (pGain == NULL) return RET_NULL_POINTER;
    memcpy(pGain, &pFLIR_BOSONCtx->SensorGain, sizeof(IsiSensorGain_t));

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSetGainIss(IsiSensorHandle_t handle, IsiSensorGain_t *pGain) {
    int      ret = 0;
    uint32_t LongGain;
    uint32_t Gain;
    uint32_t ShortGain;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (pGain == NULL) return RET_NULL_POINTER;

    pFLIR_BOSONCtx->SensorGain.expoFrmType = pGain->expoFrmType;
    switch (pGain->expoFrmType) {
    case ISI_EXPO_FRAME_TYPE_1FRAME:
        Gain = pGain->gain.linearGainParas;
        if (pFLIR_BOSONCtx->SensorGain.gain.linearGainParas != Gain) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_GAIN, &Gain);
            TRACE(FLIR_BOSON_ERROR, "liaoming[%s:%d] Gain:%d\n", __func__, __LINE__, Gain);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor linear gain error!\n", __func__);
                return RET_FAILURE;
            }
        }
        pFLIR_BOSONCtx->SensorGain.gain.linearGainParas = pGain->gain.linearGainParas;
        TRACE(FLIR_BOSON_INFO, "%s set linear gain %d\n", __func__, pGain->gain.linearGainParas);
        break;
    case ISI_EXPO_FRAME_TYPE_2FRAMES:
        Gain = pGain->gain.dualGainParas.dualGain;
        if (pFLIR_BOSONCtx->SensorGain.gain.dualGainParas.dualGain != Gain) {
            if (pFLIR_BOSONCtx->CurMode.stitching_mode != SENSOR_STITCHING_DUAL_DCG_NOWAIT) {
                ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_GAIN, &Gain);
                TRACE(FLIR_BOSON_ERROR, "liaoming[%s:%d] Gain:%d\n", __func__, __LINE__, Gain);
            } else {
                ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_LONG_GAIN, &Gain);
            }
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor dual gain error!\n", __func__);
                return RET_FAILURE;
            }
        }

        ShortGain = pGain->gain.dualGainParas.dualSGain;
        if (pFLIR_BOSONCtx->SensorGain.gain.dualGainParas.dualSGain != ShortGain) {
            if (pFLIR_BOSONCtx->CurMode.stitching_mode != SENSOR_STITCHING_DUAL_DCG_NOWAIT) {
                ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_VSGAIN, &ShortGain);
            } else {
                ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_GAIN, &ShortGain);
                TRACE(FLIR_BOSON_ERROR, "liaoming[%s:%d] ShortGain:%d\n", __func__, __LINE__, ShortGain);
            }
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor dual vs gain error!\n", __func__);
                return RET_FAILURE;
            }
        }
        TRACE(FLIR_BOSON_INFO, "%s:set gain%d short gain %d!\n", __func__, Gain, ShortGain);
        pFLIR_BOSONCtx->SensorGain.gain.dualGainParas.dualGain  = Gain;
        pFLIR_BOSONCtx->SensorGain.gain.dualGainParas.dualSGain = ShortGain;
        break;
    case ISI_EXPO_FRAME_TYPE_3FRAMES:
        LongGain = pGain->gain.triGainParas.triLGain;
        if (pFLIR_BOSONCtx->SensorGain.gain.triGainParas.triLGain != LongGain) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_LONG_GAIN, &LongGain);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor tri gain error!\n", __func__);
                return RET_FAILURE;
            }
        }
        Gain = pGain->gain.triGainParas.triGain;
        if (pFLIR_BOSONCtx->SensorGain.gain.triGainParas.triGain != Gain) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_GAIN, &Gain);
            TRACE(FLIR_BOSON_ERROR, "liaoming[%s:%d] Gain:%d\n", __func__, __LINE__, Gain);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor tri gain error!\n", __func__);
                return RET_FAILURE;
            }
        }

        ShortGain = pGain->gain.triGainParas.triSGain;
        if (pFLIR_BOSONCtx->SensorGain.gain.triGainParas.triSGain != ShortGain) {
            ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_VSGAIN, &ShortGain);
            if (ret != 0) {
                TRACE(FLIR_BOSON_ERROR, "%s:set sensor tri vs gain error!\n", __func__);
                return RET_FAILURE;
            }
        }
        TRACE(FLIR_BOSON_INFO, "%s:set long gain %d gain%d short gain %d!\n", __func__, LongGain, Gain, ShortGain);
        pFLIR_BOSONCtx->SensorGain.gain.triGainParas.triLGain = LongGain;
        pFLIR_BOSONCtx->SensorGain.gain.triGainParas.triGain  = Gain;
        pFLIR_BOSONCtx->SensorGain.gain.triGainParas.triSGain = ShortGain;
        break;
    default: return RET_FAILURE; break;
    }

    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetSensorFpsIss(IsiSensorHandle_t handle, uint32_t *pfps) {
    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    if (pfps == NULL) return RET_NULL_POINTER;

    *pfps = pFLIR_BOSONCtx->CurMode.ae_info.cur_fps;

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSetSensorFpsIss(IsiSensorHandle_t handle, uint32_t fps) {
    int ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_FPS, &fps);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s:set sensor fps error!\n", __func__);
        return RET_FAILURE;
    }
    struct vvcam_mode_info_s SensorMode;
    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_G_SENSOR_MODE, &SensorMode);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s:get sensor mode error!\n", __func__);
        return RET_FAILURE;
    }
    memcpy(&pFLIR_BOSONCtx->CurMode, &SensorMode, sizeof(struct vvcam_mode_info_s));
    FLIR_BOSON_UpdateIsiAEInfo(handle);

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}
static RESULT FLIR_BOSON_IsiSetSensorAfpsLimitsIss(IsiSensorHandle_t handle, uint32_t minAfps) {
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    if ((minAfps > pFLIR_BOSONCtx->CurMode.ae_info.max_fps) || (minAfps < pFLIR_BOSONCtx->CurMode.ae_info.min_fps)) return RET_FAILURE;
    pFLIR_BOSONCtx->minAfps                  = minAfps;
    pFLIR_BOSONCtx->CurMode.ae_info.min_afps = minAfps;

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetSensorIspStatusIss(IsiSensorHandle_t handle, IsiSensorIspStatus_t *pSensorIspStatus) {
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    if (pFLIR_BOSONCtx->CurMode.hdr_mode == SENSOR_MODE_HDR_NATIVE) {
        pSensorIspStatus->useSensorAWB = true;
        pSensorIspStatus->useSensorBLC = true;
    } else {
        pSensorIspStatus->useSensorAWB = false;
        pSensorIspStatus->useSensorBLC = false;
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

#ifndef ISI_LITE
static RESULT FLIR_BOSON_IsiSensorSetWBIss(IsiSensorHandle_t handle, IsiSensorWB_t *pWb) {
    int32_t ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s: (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    if (pWb == NULL) return RET_NULL_POINTER;

    struct sensor_white_balance_s SensorWb;
    SensorWb.r_gain  = pWb->r_gain;
    SensorWb.gr_gain = pWb->gr_gain;
    SensorWb.gb_gain = pWb->gb_gain;
    SensorWb.b_gain  = pWb->b_gain;
    ret              = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_WB, &SensorWb);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: set wb error\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSetTestPatternIss(IsiSensorHandle_t handle, IsiSensorTpgMode_e tpgMode) {
    int32_t ret = 0;

    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;

    struct sensor_test_pattern_s TestPattern;
    if (tpgMode == ISI_TPG_DISABLE) {
        TestPattern.enable  = 0;
        TestPattern.pattern = 0;
    } else {
        TestPattern.enable  = 1;
        TestPattern.pattern = (uint32_t)tpgMode - 1;
    }

    ret = ioctl(pHalCtx->sensor_fd, VVSENSORIOC_S_TEST_PATTERN, &TestPattern);
    if (ret != 0) {
        TRACE(FLIR_BOSON_ERROR, "%s: set test pattern %d error\n", __func__, tpgMode);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: test pattern enable[%d] mode[%d]\n", __func__, TestPattern.enable, TestPattern.pattern);

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);

    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiFocusSetupIss(IsiSensorHandle_t handle) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    HalContext_t         *pHalCtx        = (HalContext_t *)pFLIR_BOSONCtx->IsiCtx.HalHandle;
    vvcam_lens_t          pfocus_lens;

    if (ioctl(pHalCtx->sensor_fd, VVSENSORIOC_G_LENS, &pfocus_lens) < 0) {
        TRACE(FLIR_BOSON_ERROR, "%s  sensor do not have focus-lens \n", __func__);
        return RET_NOTSUPP;
    }

    if (pFLIR_BOSONCtx->motor_fd <= 0) {
        pFLIR_BOSONCtx->motor_fd = OpenMotorDevice(&pfocus_lens);
        if (pFLIR_BOSONCtx->motor_fd < 0) {
            TRACE(FLIR_BOSON_ERROR, "%s open sensor focus-lens fail\n", __func__);
            return RET_FAILURE;
        }
    } else {
        TRACE(FLIR_BOSON_INFO, "%s sensor focus-lens already open\n", __func__);
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);
    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiFocusReleaseIss(IsiSensorHandle_t handle) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    if (pFLIR_BOSONCtx->motor_fd <= 0) { return RET_NOTSUPP; }

    if (close(pFLIR_BOSONCtx->motor_fd) < 0) {
        TRACE(FLIR_BOSON_ERROR, "%s close motor device failed\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);
    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiFocusGetIss(IsiSensorHandle_t handle, IsiFocusPos_t *pPos) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    struct v4l2_control   ctrl;
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    if (pFLIR_BOSONCtx->motor_fd <= 0) { return RET_NOTSUPP; }

    memset(&ctrl, 0, sizeof(ctrl));
    if (pFLIR_BOSONCtx->focus_mode & (1 << (pPos->mode))) {
        if (pPos->mode == ISI_FOUCUS_MODE_ABSOLUTE) {
            ctrl.id = V4L2_CID_FOCUS_ABSOLUTE;
        } else if (pPos->mode == ISI_FOUCUS_MODE_RELATIVE) {
            ctrl.id = V4L2_CID_FOCUS_RELATIVE;
        }
    } else {
        TRACE(FLIR_BOSON_ERROR, "%s invalid Focus mode %d\n", __func__, pPos->mode);
        return RET_FAILURE;
    }

    if (ioctl(pFLIR_BOSONCtx->motor_fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        TRACE(FLIR_BOSON_ERROR, "%s get moto pos failed\n", __func__);
        return RET_FAILURE;
    }

    pPos->Pos = ctrl.value;
    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);
    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiFocusSetIss(IsiSensorHandle_t handle, IsiFocusPos_t *pPos) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    struct v4l2_control   ctrl;
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    if (pFLIR_BOSONCtx->motor_fd <= 0) { return RET_NOTSUPP; }

    memset(&ctrl, 0, sizeof(ctrl));
    if (pFLIR_BOSONCtx->focus_mode & (1 << (pPos->mode))) {
        if (pPos->mode == ISI_FOUCUS_MODE_ABSOLUTE) {
            ctrl.id    = V4L2_CID_FOCUS_ABSOLUTE;
            ctrl.value = pPos->Pos;
        } else if (pPos->mode == ISI_FOUCUS_MODE_RELATIVE) {
            ctrl.id    = V4L2_CID_FOCUS_RELATIVE;
            ctrl.value = pPos->Pos;
        }
    } else {
        TRACE(FLIR_BOSON_ERROR, "%s invalid Focus mode %d\n", __func__, pPos->mode);
        return RET_FAILURE;
    }

    if (ioctl(pFLIR_BOSONCtx->motor_fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        TRACE(FLIR_BOSON_ERROR, "%s set moto pos failed\n", __func__);
        return RET_FAILURE;
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);
    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiGetFocusCalibrateIss(IsiSensorHandle_t handle, IsiFoucsCalibAttr_t *pFocusCalib) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);
    struct v4l2_queryctrl qctrl;
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;
    RESULT                result         = RET_SUCCESS;

    if (pFLIR_BOSONCtx->motor_fd <= 0) { return RET_NOTSUPP; }

    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = V4L2_CID_FOCUS_ABSOLUTE;
    if (ioctl(pFLIR_BOSONCtx->motor_fd, VIDIOC_QUERYCTRL, &qctrl) >= 0) {
        pFLIR_BOSONCtx->focus_mode |= 1 << ISI_FOUCUS_MODE_ABSOLUTE;
        pFocusCalib->minPos  = qctrl.minimum;
        pFocusCalib->maxPos  = qctrl.maximum;
        pFocusCalib->minStep = qctrl.step;
    } else {
        qctrl.id = V4L2_CID_FOCUS_RELATIVE;
        if (ioctl(pFLIR_BOSONCtx->motor_fd, VIDIOC_QUERYCTRL, &qctrl) >= 0) {
            pFLIR_BOSONCtx->focus_mode |= 1 << ISI_FOUCUS_MODE_RELATIVE;
            pFocusCalib->minPos  = qctrl.minimum;
            pFocusCalib->maxPos  = qctrl.maximum;
            pFocusCalib->minStep = qctrl.step;
        } else {
            result = RET_FAILURE;
        }
    }

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);
    return result;
}

static RESULT FLIR_BOSON_IsiGetAeStartExposureIs(IsiSensorHandle_t handle, uint64_t *pExposure) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    if (pFLIR_BOSONCtx->AEStartExposure == 0) {
        pFLIR_BOSONCtx->AEStartExposure =
            (uint64_t)pFLIR_BOSONCtx->CurMode.ae_info.start_exposure * pFLIR_BOSONCtx->CurMode.ae_info.one_line_exp_time_ns / 1000;
    }
    *pExposure = pFLIR_BOSONCtx->AEStartExposure;
    TRACE(FLIR_BOSON_INFO, "%s:get start exposure %d\n", __func__, pFLIR_BOSONCtx->AEStartExposure);

    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);
    return RET_SUCCESS;
}

static RESULT FLIR_BOSON_IsiSetAeStartExposureIs(IsiSensorHandle_t handle, uint64_t exposure) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);
    FLIR_BOSON_Context_t *pFLIR_BOSONCtx = (FLIR_BOSON_Context_t *)handle;

    pFLIR_BOSONCtx->AEStartExposure = exposure;
    TRACE(FLIR_BOSON_INFO, "set start exposure %d\n", __func__, pFLIR_BOSONCtx->AEStartExposure);
    TRACE(FLIR_BOSON_INFO, "%s: (exit)\n", __func__);
    return RET_SUCCESS;
}
#endif

RESULT FLIR_BOSON_IsiGetSensorIss(IsiSensor_t *pIsiSensor) {
    TRACE(FLIR_BOSON_INFO, "%s (enter)\n", __func__);

    if (pIsiSensor == NULL) return RET_NULL_POINTER;
    pIsiSensor->pszName                      = SensorName;
    pIsiSensor->pIsiSensorSetPowerIss        = FLIR_BOSON_IsiSensorSetPowerIss;
    pIsiSensor->pIsiCreateSensorIss          = FLIR_BOSON_IsiCreateSensorIss;
    pIsiSensor->pIsiReleaseSensorIss         = FLIR_BOSON_IsiReleaseSensorIss;
    pIsiSensor->pIsiRegisterReadIss          = FLIR_BOSON_IsiRegisterReadIss;
    pIsiSensor->pIsiRegisterWriteIss         = FLIR_BOSON_IsiRegisterWriteIss;
    pIsiSensor->pIsiGetSensorModeIss         = FLIR_BOSON_IsiGetSensorModeIss;
    pIsiSensor->pIsiSetSensorModeIss         = FLIR_BOSON_IsiSetSensorModeIss;
    pIsiSensor->pIsiQuerySensorIss           = FLIR_BOSON_IsiQuerySensorIss;
    pIsiSensor->pIsiGetCapsIss               = FLIR_BOSON_IsiGetCapsIss;
    pIsiSensor->pIsiSetupSensorIss           = FLIR_BOSON_IsiSetupSensorIss;
    pIsiSensor->pIsiGetSensorRevisionIss     = FLIR_BOSON_IsiGetSensorRevisionIss;
    pIsiSensor->pIsiCheckSensorConnectionIss = FLIR_BOSON_IsiCheckSensorConnectionIss;
    pIsiSensor->pIsiSensorSetStreamingIss    = FLIR_BOSON_IsiSensorSetStreamingIss;
    pIsiSensor->pIsiGetAeInfoIss             = FLIR_BOSON_IsiGetAeInfoIss;
    pIsiSensor->pIsiGetIntegrationTimeIss    = FLIR_BOSON_IsiGetIntegrationTimeIss;
    pIsiSensor->pIsiSetIntegrationTimeIss    = FLIR_BOSON_IsiSetIntegrationTimeIss;
    pIsiSensor->pIsiGetGainIss               = FLIR_BOSON_IsiGetGainIss;
    pIsiSensor->pIsiSetGainIss               = FLIR_BOSON_IsiSetGainIss;
    pIsiSensor->pIsiGetSensorFpsIss          = FLIR_BOSON_IsiGetSensorFpsIss;
    pIsiSensor->pIsiSetSensorFpsIss          = FLIR_BOSON_IsiSetSensorFpsIss;
    pIsiSensor->pIsiSetSensorAfpsLimitsIss   = FLIR_BOSON_IsiSetSensorAfpsLimitsIss;
    pIsiSensor->pIsiGetSensorIspStatusIss    = FLIR_BOSON_IsiGetSensorIspStatusIss;
#ifndef ISI_LITE
    pIsiSensor->pIsiSensorSetWBIss         = FLIR_BOSON_IsiSensorSetWBIss;
    pIsiSensor->pIsiActivateTestPatternIss = FLIR_BOSON_IsiSetTestPatternIss;
    pIsiSensor->pIsiFocusSetupIss          = FLIR_BOSON_IsiFocusSetupIss;
    pIsiSensor->pIsiFocusReleaseIss        = FLIR_BOSON_IsiFocusReleaseIss;
    pIsiSensor->pIsiFocusSetIss            = FLIR_BOSON_IsiFocusSetIss;
    pIsiSensor->pIsiFocusGetIss            = FLIR_BOSON_IsiFocusGetIss;
    pIsiSensor->pIsiGetFocusCalibrateIss   = FLIR_BOSON_IsiGetFocusCalibrateIss;
    pIsiSensor->pIsiSetAeStartExposureIss  = FLIR_BOSON_IsiSetAeStartExposureIs;
    pIsiSensor->pIsiGetAeStartExposureIss  = FLIR_BOSON_IsiGetAeStartExposureIs;
#endif
    TRACE(FLIR_BOSON_INFO, "%s (exit)\n", __func__);
    return RET_SUCCESS;
}

/*****************************************************************************
* each sensor driver need declare this struct for isi load
*****************************************************************************/
IsiCamDrvConfig_t IsiCamDrvConfig = {
    .CameraDriverID     = 0x2770,
    .pIsiHalQuerySensor = FLIR_BOSON_IsiHalQuerySensorIss,
    .pfIsiGetSensorIss  = FLIR_BOSON_IsiGetSensorIss,
};
