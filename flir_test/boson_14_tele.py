"""

Open Boson camera at /dev/video1 GRAY16 format and capture a frame.
Save the frame (16bit) image and its normalized outputs to 16bit and 8bit image files.
And calculate PNSR among them to detect which ones are closer and which ones are more different.

Copyright (C) 2026 Videology
Programmed by Jianping Ye <jye@videologyinc.com>

Feb 12. Added 16bit to 8bit to 16bit conversion and PSNR calculation for raw, scaled, and normalized images.

"""

import cv2
import numpy as np
from glob import glob
from PIL import Image

import struct


def show_telemetry(tel_line):
    tel_line = tel_line.astype(
        np.uint8
    ).tobytes()  # telemetry line seems to be bytes packed into the 14-bit values...
    cam_temp = struct.unpack_from("<H", tel_line, 96)[0]
    cam_temp = (cam_temp / 10.0) - 273.15
    print(f"Cam temp: {cam_temp}")


def calculate_psnr(src, tgt):
    return cv2.PSNR(src, tgt)


# Save 16bit image to different output files.
def save_images(img, prefix):
    height = img.shape[0]
    img2 = img[:-2, :] if height == 514 else img
    print("min, max pixel value = ", np.amin(img2), ",", np.amax(img2))

    # Save 16bit img raw
    cv2.imwrite(prefix + "_16bit.png", img2)

    image_16bit_normalized = cv2.normalize(
        img2, None, 0, 65535, cv2.NORM_MINMAX
    ).astype(np.uint16)
    cv2.imwrite(prefix + "_normalize_16bit.png", image_16bit_normalized)

    # Save image after simple scale
    image_16bit_scaled = cv2.convertScaleAbs(img2, alpha=(4.0)).astype(np.uint16)
    cv2.imwrite(prefix + "_scale_mul4_16bit.png", image_16bit_scaled)

    # Normalize to 8bit and save.
    image_8bit = cv2.normalize(img2, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)
    cv2.imwrite(prefix + "_normalize_8bit.png", image_8bit)

    # Scale to 8bit and save.
    image_scale = cv2.convertScaleAbs(img2, alpha=(1.0 / 64.0)).astype(np.uint8)
    cv2.imwrite(prefix + "_scale_div64_8bit.png", image_scale)

    image_scale_16bit = cv2.convertScaleAbs(image_scale, alpha=(256.0)).astype(
        np.uint16
    )
    image_norm_scale_16bit = np.uint16(image_8bit) * 257
    cv2.imwrite(prefix + "_normalize_8bit_scale256_16bit.png", image_norm_scale_16bit)

    # Show and compare PSNR
    # 16bit psnr
    print(
        "PSNR 16bit raw to normalized    = ",
        calculate_psnr(image_16bit_normalized, img2),
    )
    print(
        "PSNR 16bit raw to scaled        = ", calculate_psnr(image_16bit_scaled, img2)
    )
    print(
        "PSNR 16bit scaled to normalized = ",
        calculate_psnr(image_16bit_normalized, image_16bit_scaled),
    )
    # 8bit psnr
    print("PSNR 8bit scaled to normalized  = ", calculate_psnr(image_scale, image_8bit))

    # Scale to scale
    print(
        "PSNR 8bit to 16bit scaled        = ",
        calculate_psnr(image_scale_16bit, image_16bit_scaled),
    )
    # Normalize to nomalize
    print(
        "PSNR 8bit to 16bit normalized    = ",
        calculate_psnr(image_16bit_normalized, image_norm_scale_16bit),
    )


# Open a Boson camera at 640 x 514 to get telemetry info from the extra 2 lines ;-)
def boson_show_telemetry(camera_number=0, height=514, prefix=""):
    cap = cv2.VideoCapture(camera_number, cv2.CAP_V4L2)
    if not cap.isOpened():
        raise SystemExit("Failed to open camera")

    try:
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_CONVERT_RGB, 0)
        cap.set(cv2.CAP_PROP_FPS, 60)
        cap.set(
            cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"Y16 ")
        )  # can get this in gstreamer with "formatGRAY16_LE
        fourcc = int(cap.get(cv2.CAP_PROP_FOURCC))
        fcc = "".join([chr((fourcc >> 8 * i) & 0xFF) for i in range(4)])
        print(f"fcc: {fcc}")

        ret, img = cap.read()
        if img is None:
            print("something went wrong")
        else:
            # display(Image.fromarray(img))
            # display(Image.fromarray(cv2.normalize(img[:-2,:], None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)))
            # normalise so we can actually see something
            if height == 514:
                tel_line = img[-2, :]
                show_telemetry(tel_line)
            if prefix:
                print(img.shape, img.dtype)
                save_images(img, prefix)

    finally:
        cap.release()


# main function with boson at /dev/video1
# boson_show_telemetry(1, 514, "boson_640x514")

boson_show_telemetry(1, 512, "boson_640x512")
