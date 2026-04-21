## Flir Boson Device Driver Unit Tests

***
#### Programs to do unit tests of a Boson camera sensor connected at csi1 of Scailx device.

#### 1. './show_boson_sensor_dimension.sh' => Show Boson camera sensor dimension.

#### 2. 'python3 detect_boson.py' => Detect Boson camera device path. 
	
#### 3. First need to create subfolder to store test image files.

####	'mkdir img'
####	'python3 boson_formats_opencv.py'	=> Go through ALL supported formats and grab + save image files.

#### 4.	'./boson_gst_opencv 1 640 512 GRAY8'	=> Bash script to test one format of gstreamer.

 
***

