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
                img2 = img[:-2, :] if height == 514 else img
                print("min, max pixel value = ", np.amin(img2), ",", np.amax(img2))
                # Save 16bit img before normalize
                cv2.imwrite(prefix + "_16bit.png", img2)
                image_8bit = cv2.normalize(img2, None, 0, 255, cv2.NORM_MINMAX).astype(
                    np.uint8
                )
                # Save img after normalize
                cv2.imwrite(prefix + "_normalize_8bit.png", image_8bit)
                # Save image after simple scale
                image_scale = cv2.convertScaleAbs(img2, alpha=(1.0 / 64.0)).astype(np.uint8)
                cv2.imwrite(prefix + "_scale_64_8bit.png", image_scale)

    finally:
        cap.release()


# main function with boson at /dev/video1
boson_show_telemetry(1, 514, "boson_640x514")

boson_show_telemetry(1, 512, "boson_640x512")
