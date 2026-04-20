## Flir Boson Device Driver on Scailx

***
#### How to build and test the device driver from this repository on Scailx device.

#### Step 1. Follow steps same as Global Shutter camera ar0234's kernel driver build preparation on Scailx Device.
	
####	'cd /usr/src/kernel'
####	'gunzip < /proc/config.gz > .config'
####	'make oldconfig'
####	'make prepare'
####	'make scripts'

#### Step 2. Now checkout this flir boson kernel repository into Scailx device.

####	'cd /root/flir_boson_kernel_module/flir_boson_v4l2'
####	'make'
	
####	If no errors, 'make modules_install' to install the new Boson device driver.

***

#### Step 3. Test the new driver.
	
####	3.1. First make sure the new driver ko file is in place.
	
####	'ls -lt /lib/modules/6.6.52-lts-next/updates/flir-boson.ko'
	
####	We should see its date time is new and matching *.ko in local folder.
####	Rebot the scailx device. 'reboot'
		
####	After Scailx reboots, wait for a few seconds. 
####	'lsmod' to see loaded driver modules if a Boson camera sensor is connected to csi1.
####	Make sure "imx8_media_dev" and "flir_boson" is on the list.

***

####	3.2. Run some basic v4l2 commands to show Boson camera info.
 
####	'v4l2-ctl --list-devices'	
####	It should show Boson camera is at /dev/video0 or /dev/video1, etc.

####	'v4l2-ctl -d /dev/video0 --list-formats-ext'
####	Please use correct Boson camera path in previous step, /de/video0, /dev/video1, etc.

***

####	3.3. Run bash script or equivalent similar gst-launch command to see camera video stream on web 8091 port.

####	In repository branch's ~/flir_test/ folder, run the bash script

####	'./gst_boson_format.sh <camera_path_id> width height format'

####	For example, './gst_boson_format.sh 1 320 256 GRAY8'

####	If no errors, we should see camera video stream on web page port 8091, for example, 'http://sca-demo.local:8091/'.

***

