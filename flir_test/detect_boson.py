#!/usr/bin/env python3

"""

File:   detect_boson.py

2026.0421.  Detect boson camera path.

By:			jye@videologyinc.com

"""

from vdlg_lvds.detect_cameras_live import detect_cameras

# Main program to get current cameras and detect Boson path.

def detect_boson_path():
	camera_status = detect_cameras()

	for key, val in camera_status.items():
		if val[0]=="boson":
			print("Boson is at ", key, val[1])
			return True
	return False
	
ret = detect_boson_path()
if not ret:
	print("No Boson cameras detected.")
	
