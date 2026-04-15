#!/bin/bash

if [ $# -lt 4 ]; then
  echo "Usage: $0 </dev/video id> width height format"
  exit 1
fi

echo "Camera device is /dev/video$1"

echo Run gst-launch to test format of the Boson camera.

echo gst-launch-1.0 v4l2src device=/dev/video$1 ! video/x-raw, width=$2, height=$3, format=$4, framerate=60/1 ! videoconvert ! autovideosink

gst-launch-1.0 v4l2src device=/dev/video$1 ! video/x-raw, width=$2, height=$3, format=$4, framerate=60/1 ! videoconvert ! autovideosink

