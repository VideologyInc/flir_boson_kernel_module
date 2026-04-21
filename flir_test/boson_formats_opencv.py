"""

Open Boson camera at each resolution and format to verify they are correct.

Copyright (C) 2026 Videology
Programmed by Jianping Ye <jye@videologyinc.com>

Apr 21. Added function to set resolution and format by fourcc from v4l2-ctl --list-formats-ext and grab a frame.

"""

import cv2
import numpy as np

from vdlg_lvds.v4l2_detect_formats import parse_v4l2_formats

Bayer8_Fourcc_List = ["BA81", "GBRG", "GRBG", "RGGB"]
Bayer10_Fourcc_List = ["BG10", "GB10", "BA10", "RG10"]
Bayer12_Fourcc_List = ["BG12", "GB12", "BA12", "RG12"]

def is_bayer8(fmt_fourcc):
    return fmt_fourcc in Bayer8_Fourcc_List

def is_bayer10(fmt_fourcc):
    return fmt_fourcc in Bayer10_Fourcc_List

def is_bayer12(fmt_fourcc):
    return fmt_fourcc in Bayer12_Fourcc_List

# Open a Boson camera at w x h, fps = 60, and fourcc format, grab a frame and save.
# return True/False, filename
def boson_grab_save(camera_number, width, height, format_fourcc, fps = 60, prefix=""):

    cap = cv2.VideoCapture(camera_number, cv2.CAP_V4L2)
    if not cap.isOpened():
        print("Failed to open camera")
        return False, ""

    name = f"{prefix}_{width}x{height}x{fps}_{format_fourcc}.png" 
    ret = False    
    try:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
        cap.set(cv2.CAP_PROP_FPS, fps)
        cap.set(
            cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*format_fourcc)
        )
        # fourcc = int(cap.get(cv2.CAP_PROP_FOURCC))
        # fcc = "".join([chr((fourcc >> 8 * i) & 0xFF) for i in range(4)])
        # print(f"fcc: {fcc}")

        ret_cap, img = cap.read()

        if img is None:
            print("Grab frame error")
        else:
            ret = True
            if prefix:
                if (format_fourcc=="YUYV"):
                    img_bgr = cv2.cvtColor(img, cv2.COLOR_YUV2BGR_YUYV)
                    ret = cv2.imwrite(name, img_bgr)
                    print(img_bgr.shape, img_bgr.dtype)
                elif (format_fourcc=="NV12"):
                    img_bgr = cv2.cvtColor(img, cv2.COLOR_YUV2BGR_NV12)
                    ret = cv2.imwrite(name, img_bgr)
                    print(img_bgr.shape, img_bgr.dtype)
                elif (format_fourcc=="YM24"):
                    img_bgr = img.reshape(height, width, 3)
                    ret = cv2.imwrite(name, img_bgr)
                    print(img_bgr.shape, img_bgr.dtype)
                elif (format_fourcc=="YUV4"):
                    # 4 channel case: need reshape
                    img_bgra = img.reshape(height, width, 4)
                    print(img_bgra.shape)
                    ret = cv2.imwrite(name, img_bgra)
                elif (format_fourcc=="RGBP"):
                    # RGB 5-6-5: 16bit to RGB
                    img16 = img.view(np.uint16).reshape(height, width)
                    ret = cv2.imwrite(name, img16)
                    print(img16.shape, img16.dtype)
                elif is_bayer8(format_fourcc) or format_fourcc=="JPEG":
                    # Bayer8 is similar to Gray8
                    img_bayer = img.reshape(height, width)
                    ret = cv2.imwrite(name, img_bayer)
                    print(img_bayer.shape, img_bayer.dtype)
                elif is_bayer10(format_fourcc) or is_bayer12(format_fourcc):
                    # Bayer10 and Bayer12 is similar to Gray16
                    img_bayer = img.view(np.uint16).reshape(height, width)
                    ret = cv2.imwrite(name, img_bayer)
                    print(img_bayer.shape, img_bayer.dtype)
                elif (format_fourcc=="NM12"):
                    print(img.shape, img.dtype)
                else:
                    ret = cv2.imwrite(name, img)
                    print(img.shape, img.dtype)

    except:
        return False, ""
    finally:
        cap.release()
    return ret, name

format_list = parse_v4l2_formats("/dev/video1")
prefix = "img/unittest"

for fmt_dict in format_list:
    # print(fmt_dict)
    fmt_fourcc = fmt_dict["pixelformat"]
    for sz in fmt_dict["sizes"]:
        w = sz["width"]
        h = sz["height"]
        fps_list = sz["fps"]
        if w==0 or h==0 or fps_list==[]:
            continue
        fps = int(fps_list[0])
        # skip NM12
        if fmt_fourcc=="NM12":
            continue

        fmt_str = f"{w}x{h}x{fps}, {fmt_fourcc}"
        print(fmt_str)
        ret, name = boson_grab_save(1, w, h, fmt_fourcc, fps, prefix)
        print(ret, name)
        print()