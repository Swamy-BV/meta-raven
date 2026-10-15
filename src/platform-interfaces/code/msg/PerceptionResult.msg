# Authoritative result: model identity and standard vision payload travel together.
std_msgs/Header header
string instance_id
uint64 model_generation
uint64 job_sequence
string model_id
string model_version
string model_sha256
uint64 frame_sequence
uint64 source_pts_ns
bool source_pts_valid
# pipeline_clock is a wall-time estimate of PTS; buffer_receipt is not exposure time.
string timestamp_basis
uint32 image_width
uint32 image_height
string task
vision_msgs/Classification classification
vision_msgs/Detection2DArray detections
