import gi
gi.require_version('GstVideo', '1.0')
from gi.repository import GstVideo

# Convert a FOURCC integer to GStreamer VideoFormat
# Example: 'YUY2' as an integer
fourcc_int = sum(ord(c) << (i * 8) for i, c in enumerate('NV12'))
gst_format = GstVideo.video_format_from_fourcc(fourcc_int)

# Get the string name (e.g., "YUY2")
format_name = GstVideo.video_format_to_string(gst_format)
print(f"GStreamer format: {format_name}")
