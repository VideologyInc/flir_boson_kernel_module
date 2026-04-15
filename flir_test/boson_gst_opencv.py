"""

Open Boson camera using gstreamer pipeline and save a frame to image file.

Copyright (C) 2026 Videology
Programmed by Jianping Ye <jye@videologyinc.com>

"""

import argparse
import cv2
import numpy as np


def boson_grab_save(args):

    fps = 60

    pipeline = (
        f"v4l2src device={args.device} ! "
        f"video/x-raw, width={args.width}, height={args.height}, framerate={fps}/1 ! "
        "videoconvert ! "
        f"video/x-raw, format={args.format} ! appsink"
    )
    cap = cv2.VideoCapture(pipeline, cv2.CAP_GSTREAMER)

    if not cap.isOpened():
        raise SystemExit("Failed to open camera")

    try:

        ret, img = cap.read()
        if img is None:
            print("something went wrong")
        else:
            # Save frame to file
            name = args.prefix + f"_{args.width}x{args.height}_{args.format}.png"
            cv2.imwrite(name, img)

    finally:
        cap.release()


def main():
    parser = argparse.ArgumentParser(description="Boson Camera Grab a Frame")
    parser.add_argument("-d", "--device", default="/dev/video0", type=str, help="Camera device path.")
    parser.add_argument("-f", "--format", default="GRAY8", type=str, help="Camera gst format string.")
    parser.add_argument("-w", "--width", default=640, type=int, help="Frame width.")
    # -h is for --help and cannot be used ;-)
    parser.add_argument("--height", default=512, type=int, help="Frame height.")

    parser.add_argument("-p", "--prefix", default="frame", type=str, help="Image file prefix to save the frame.")

    args = parser.parse_args()

    boson_grab_save(args)

if __name__ == "__main__":
    main()
