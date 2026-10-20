// SPDX-License-Identifier: AGPL-3.0-only

#include "node.h"

#include <rmw/qos_profiles.h>

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "std_msgs/msg/header.hpp"
#include <image_transport/image_transport.hpp>
#include <rclcpp/expand_topic_or_service_name.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/parameter.hpp>
#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>
#include <rclcpp_action/create_server.hpp>
#include <rclcpp_action/types.hpp>

#include "application/session.h"
#include "core/types.h"
#include "ports/capture_backend.h"
#include "ports/media_store.h"
#include "settings.h"

namespace raven::media {
namespace srv = raven_interfaces::srv;

namespace {
constexpr auto kMicrosPerSecond = 1000000ULL;
constexpr auto kNanosPerMicro = 1000ULL;
constexpr auto kNanosPerSecond = 1000000000ULL;
constexpr auto kChannels = 3U;
constexpr auto kEventHistory = 10;
constexpr auto kMaxWidth = 4096U;
constexpr auto kMaxHeight = 3072U;
constexpr auto kMaxInputBytes = 4096ULL * 3072 * kChannels;
constexpr auto kMaxRequestId = 128U;
constexpr auto kPollInterval = std::chrono::milliseconds(10);

template <class Reply, class Operation>
void Perform(Reply& reply, const Operation& operation) {
  try {
    operation();
    reply.success = true;
  } catch (const std::exception& error) {
    reply.success = false;
    reply.error = error.what();
  }
}

std_msgs::msg::Header Header(const Photo& photo) {
  std_msgs::msg::Header header;
  header.frame_id = photo.frame_id;
  const auto stamp = photo.source_stamp_ns != 0
                         ? photo.source_stamp_ns
                         : photo.stamp_us * kNanosPerMicro;
  header.stamp.sec = static_cast<std::int32_t>(stamp / kNanosPerSecond);
  header.stamp.nanosec = static_cast<std::uint32_t>(stamp % kNanosPerSecond);
  return header;
}
}  // namespace

MediaCaptureNode::MediaCaptureNode(std::unique_ptr<CaptureBackend> backend,
                                   const StoreFactory& store)
    : Node("media_capture", "/raven/vehicle_1/cameras/front"),
      backend_(std::move(backend)) {
  optical_frame_ =
      declare_parameter<std::string>("optical_frame", "camera_optical_frame");
  const auto calibration_url =
      declare_parameter<std::string>("camera_info_url", "");
  camera_info_ = std::make_unique<camera_info_manager::CameraInfoManager>(
      this, "front_camera");
  if (!calibration_url.empty() &&
      !camera_info_->loadCameraInfo(calibration_url)) {
    throw std::invalid_argument("Unable to load camera_info_url");
  }
  const auto settings = ReadSettings();
  store_ = store(settings);
  session_ = std::make_unique<Session>(settings, *backend_, *store_);
  auto qos = rmw_qos_profile_sensor_data;
  qos.depth = 1;
  images_ = image_transport::create_camera_publisher(this, "image_raw", qos);
  const auto state_qos = rclcpp::QoS(1).reliable().transient_local();
  state_ =
      create_publisher<raven_interfaces::msg::MediaState>("state", state_qos);
  capabilities_ = create_publisher<raven_interfaces::msg::MediaCapabilities>(
      "capabilities", state_qos);
  events_ = create_publisher<raven_interfaces::msg::MediaCaptureEvent>(
      "capture_events", rclcpp::QoS(kEventHistory).reliable());
  health_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("health",
                                                                    state_qos);
  ConfigureServices();
  ConfigureSourceServices();
  ConfigureAction();
  ConfigureInput();
  session_->Start();
  poll_ = create_wall_timer(kPollInterval, [this] { Tick(); });
  heartbeat_ =
      create_wall_timer(std::chrono::seconds(1), [this] { PublishState(); });
  PublishCapabilities();
}

void MediaCaptureNode::ConfigureServices() {
  profile_ = create_service<srv::SetMediaProfile>(
      "configure_profile",
      [this](const srv::SetMediaProfile::Request::SharedPtr& request,
             const srv::SetMediaProfile::Response::SharedPtr& response) {
        Perform(*response, [&] {
          session_->ConfigureProfile(FromMessage(request->profile));
          auto settings = session_->Configuration();
          response->effective =
              ToMessage(SelectProfile(settings, request->profile.purpose));
        });
        PublishState();
      });
  mode_ = create_service<srv::SetMediaMode>(
      "set_mode",
      [this](const srv::SetMediaMode::Request::SharedPtr& request,
             const srv::SetMediaMode::Response::SharedPtr& response) {
        Perform(*response, [&] { session_->SetMode(request->mode); });
        PublishState();
      });
  output_ = create_service<srv::SetMediaOutput>(
      "set_output",
      [this](const srv::SetMediaOutput::Request::SharedPtr& request,
             const srv::SetMediaOutput::Response::SharedPtr& response) {
        Perform(*response, [&] {
          session_->SetOutput(request->output, request->enabled);
        });
        response->recording_id = session_->Status().recording_id;
        PublishState();
      });
}

void MediaCaptureNode::ConfigureSourceServices() {
  source_ = create_service<srv::SetMediaSource>(
      "configure_source",
      [this](const srv::SetMediaSource::Request::SharedPtr& request,
             const srv::SetMediaSource::Response::SharedPtr& response) {
        Perform(*response, [&] { ChangeSource(*request); });
        PublishState();
      });
  controls_ = create_service<srv::SetCameraControls>(
      "set_controls",
      [this](const srv::SetCameraControls::Request::SharedPtr& request,
             const srv::SetCameraControls::Response::SharedPtr& response) {
        Perform(*response, [&] {
          session_->SetControls(
              {request->auto_exposure, request->exposure_time_us,
               request->auto_white_balance, request->colour_temperature});
        });
      });
}

void MediaCaptureNode::ChangeSource(
    const srv::SetMediaSource::Request& request) {
  auto source = session_->Configuration().source;
  source.kind = request.source;
  source.uri = request.uri;
  source.camera_name = request.camera_name;
  source.image_topic = request.image_topic;
  source.camera_info_topic = request.camera_info_topic;
  if (source.kind == "ros") {
    const auto expanded = rclcpp::expand_topic_or_service_name(
        source.image_topic, get_name(), get_namespace(), false);
    if (expanded == images_.getTopic()) {
      throw std::invalid_argument(
          "ROS input cannot subscribe to its own output");
    }
  }
  session_->ConfigureSource(source);
  // The configured calibration belongs to the startup source. A new source
  // must supply its own CameraInfo or be restarted with its calibration URL.
  configured_calibration_valid_ = false;
  ConfigureInput();
  PublishCapabilities();
}

void MediaCaptureNode::ConfigureAction() {
  capture_ = rclcpp_action::create_server<Capture>(
      this, "capture_photos",
      [this](const rclcpp_action::GoalUUID&,
             const std::shared_ptr<const Capture::Goal>& goal) {
        if (goal_ || !session_->Status().ready ||
            goal->request_id.size() > kMaxRequestId) {
          return rclcpp_action::GoalResponse::REJECT;
        }
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [this](const std::shared_ptr<Goal>&) {
        session_->CancelCapture();
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [this](std::shared_ptr<Goal> goal) {
        goal_ = std::move(goal);
        const auto request = goal_->get_goal();
        const auto id = request->request_id.empty()
                            ? "ros-" + std::to_string(now().nanoseconds())
                            : request->request_id;
        try {
          session_->Capture(id, request->count, request->interval);
        } catch (const std::exception& error) {
          auto result = std::make_shared<Capture::Result>();
          result->error = error.what();
          goal_->abort(result);
          goal_.reset();
        }
      });
}

void MediaCaptureNode::ConfigureInput() {
  input_.reset();
  input_info_.reset();
  calibration_ = sensor_msgs::msg::CameraInfo{};
  const auto& source = session_->Configuration().source;
  if (source.kind != "ros") {
    return;
  }
  const auto expanded = rclcpp::expand_topic_or_service_name(
      source.image_topic, get_name(), get_namespace(), false);
  if (expanded == images_.getTopic()) {
    throw std::invalid_argument("ROS input topic loops back to camera output");
  }
  input_ = create_subscription<sensor_msgs::msg::Image>(
      source.image_topic, rclcpp::SensorDataQoS().keep_last(1),
      [this](const sensor_msgs::msg::Image::ConstSharedPtr& image) {
        ReceiveImage(*image);
      });
  input_info_ = create_subscription<sensor_msgs::msg::CameraInfo>(
      source.camera_info_topic, rclcpp::SensorDataQoS().keep_last(1),
      [this](const sensor_msgs::msg::CameraInfo::ConstSharedPtr& info) {
        calibration_ = *info;
      });
}

// ROS logging macros add nested blocks unrelated to this function's control
// flow. NOLINTNEXTLINE(readability-function-size)
void MediaCaptureNode::ReceiveImage(const sensor_msgs::msg::Image& image) {
  try {
    InputFrame(image);
  } catch (const std::exception& error) {
    RCLCPP_ERROR(get_logger(), "ROS frame rejected: %s", error.what());
  }
}

void MediaCaptureNode::InputFrame(const sensor_msgs::msg::Image& image) {
  if (image.encoding != "rgb8" || image.width == 0 || image.height == 0 ||
      image.width > kMaxWidth || image.height > kMaxHeight ||
      image.data.size() > kMaxInputBytes ||
      image.step < image.width * kChannels ||
      image.data.size() !=
          static_cast<std::size_t>(image.step) * image.height ||
      image.header.stamp.sec < 0) {
    throw std::invalid_argument(
        "ROS source requires bounded RGB8 images with valid timestamps");
  }
  raven::contracts::CameraFrame frame;
  frame.width = static_cast<int>(image.width);
  frame.height = static_cast<int>(image.height);
  frame.captured_unix_us =
      static_cast<std::uint64_t>(image.header.stamp.sec) * kMicrosPerSecond +
      image.header.stamp.nanosec / kNanosPerMicro;
  frame.source_stamp_ns =
      static_cast<std::uint64_t>(image.header.stamp.sec) * kNanosPerSecond +
      image.header.stamp.nanosec;
  frame.optical_frame = image.header.frame_id;
  const auto row = static_cast<std::size_t>(image.width) * kChannels;
  frame.data.resize(row * image.height);
  for (std::uint32_t y = 0; y < image.height; ++y) {
    std::memcpy(frame.data.data() + y * row,
                image.data.data() + static_cast<std::size_t>(y) * image.step,
                row);
  }
  backend_->PushFrame(frame);
}

void MediaCaptureNode::PublishFrame(raven::contracts::CameraFrame frame) {
  sensor_msgs::msg::Image image;
  image.header = Header(
      {{},
       {},
       frame.optical_frame.empty() ? optical_frame_ : frame.optical_frame,
       frame.captured_unix_us,
       0,
       frame.width,
       frame.height,
       frame.source_stamp_ns});
  if (frame.source_stamp_ns == 0 && !MapNativeStamp(image.header)) {
    return;
  }
  image.width = frame.width;
  image.height = frame.height;
  image.encoding = "rgb8";
  image.step = image.width * kChannels;
  image.data = std::move(frame.data);
  sensor_msgs::msg::CameraInfo info;
  info.header = image.header;
  info.width = image.width;
  info.height = image.height;
  // Configured calibration applies only at its original resolution.
  if (configured_calibration_valid_ && camera_info_->isCalibrated()) {
    const auto configured = camera_info_->getCameraInfo();
    if (configured.width == image.width && configured.height == image.height) {
      info = configured;
      info.header = image.header;
    }
  }
  // Unknown or differently sized calibration is never invented.
  if (calibration_.header.frame_id == image.header.frame_id &&
      calibration_.width == image.width &&
      calibration_.height == image.height) {
    info = calibration_;
    info.header = image.header;
  }
  TraceFrame(rclcpp::Time(image.header.stamp).nanoseconds());
  images_.publish(image, info);
}

// This single ROS logging macro expands to many generated control statements.
// NOLINTNEXTLINE(readability-function-size)
void MediaCaptureNode::TraceFrame(std::int64_t source_stamp_ns) const {
  RCLCPP_DEBUG(get_logger(),
               "timing stage=capture source_ns=%" PRId64 " ros_ns=%" PRId64
               " steady_ns=%" PRId64,
               source_stamp_ns, now().nanoseconds(),
               static_cast<std::int64_t>(
                   std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count()));
}

bool MediaCaptureNode::MapNativeStamp(std_msgs::msg::Header& header) const {
  if (!get_parameter("use_sim_time").as_bool()) {
    return true;
  }
  const auto wall = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
  const auto source = rclcpp::Time(header.stamp).nanoseconds();
  const auto stamp = now().nanoseconds() - (wall - source);
  if (source > wall || stamp <= 0) {
    return false;  // Wait for Clock; never mix wall and simulation epochs.
  }
  header.stamp = rclcpp::Time(stamp);
  return true;
}

void MediaCaptureNode::PublishPhoto(const Photo& photo) {
  raven_interfaces::msg::MediaCaptureEvent event;
  event.header = Header(photo);
  if (photo.source_stamp_ns == 0 && !MapNativeStamp(event.header)) {
    // Capture completed, but its ROS time is unknown.
    event.header.stamp.sec = 0;
    event.header.stamp.nanosec = 0;
  }
  if (event.header.frame_id.empty()) {
    event.header.frame_id = optical_frame_;
  }
  event.request_id = photo.request_id;
  event.image_index = photo.index;
  event.uri = photo.uri;
  event.width = photo.width;
  event.height = photo.height;
  event.success = true;
  events_->publish(event);
  if (goal_ && goal_->is_active()) {
    auto feedback = std::make_shared<Capture::Feedback>();
    feedback->capture = event;
    goal_->publish_feedback(feedback);
  }
}

void MediaCaptureNode::PublishNextFrame() {
  if (images_.getNumSubscribers() == 0) {
    return;
  }
  if (auto frame = backend_->TakeFrame()) {
    PublishFrame(std::move(*frame));
  }
}

void MediaCaptureNode::Tick() {
  session_->Tick();
  try {
    PublishNextFrame();
  } catch (const std::exception& error) {
    session_->Fail(error.what());
  }
  if (auto photo = session_->TakeEvent()) {
    PublishPhoto(*photo);
  }
  if (goal_ && !session_->Status().capturing) {
    auto result = std::make_shared<Capture::Result>();
    result->completed = session_->Completed();
    result->last_uri = session_->LastUri();
    result->error = session_->Status().error;
    if (goal_->is_canceling()) {
      goal_->canceled(result);
    } else if (!result->error.empty()) {
      goal_->abort(result);
    } else {
      goal_->succeed(result);
    }
    goal_.reset();
    PublishState();
  }
}

void MediaCaptureNode::PublishState() {
  raven_interfaces::msg::MediaState message;
  const auto& config = session_->Configuration();
  const auto& state = session_->Status();
  message.header.stamp = now();
  message.source = config.source.kind;
  message.mode = config.mode;
  message.ready = state.ready;
  message.recording = config.recording;
  message.streaming = config.streaming;
  message.capturing = state.capturing;
  message.image_count = state.image_count;
  message.recording_id = state.recording_id;
  message.error = state.error;
  message.free_bytes = session_->FreeBytes();
  message.total_bytes = store_->TotalBytes();
  for (const auto* profile :
       {&config.photo, &config.preview, &config.video, &config.image}) {
    message.profiles.push_back(ToMessage(*profile));
  }
  state_->publish(message);
  diagnostic_msgs::msg::DiagnosticArray health;
  health.header = message.header;
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = get_fully_qualified_name();
  status.level = state.error.empty()
                     ? diagnostic_msgs::msg::DiagnosticStatus::OK
                     : diagnostic_msgs::msg::DiagnosticStatus::ERROR;
  status.message = state.error.empty() ? "capture active" : state.error;
  health.status.push_back(status);
  health_->publish(health);
}

void MediaCaptureNode::PublishCapabilities() {
  raven_interfaces::msg::MediaCapabilities message;
  message.header.stamp = now();
  message.sources = {"test", "libcamera", "camera", "image",
                     "file", "ros",       "rtp"};
  message.modes = {"photo", "video"};
  const auto& config = session_->Configuration();
  message.hardware_encoding = config.encoder == "v4l2h264enc";
  message.independent_profiles = true;
  message.photo_during_recording = true;
  message.max_width =
      config.source.kind == "libcamera" ? config.source.width : 0;
  message.max_height =
      config.source.kind == "libcamera" ? config.source.height : 0;
  message.controls = backend_->ControlNames();
  message.limitations = {
      "Recording and streaming share one encoder and require matching profiles",
      "Profile/source/mode changes require stopped recording",
      "ROS input accepts RGB8; other pixel formats are rejected",
      "Network input accepts H.264 RTP/UDP with payload type 96",
      "Video file replay accepts H.264 video through the hardware decoder",
      "Exposure and white balance controls require a native control adapter",
      "Source/profile transitions can interrupt preview",
      "Photo JPEG encoding and ROS pixel copies use CPU",
      "No autofocus on the manual-focus IMX477"};
  capabilities_->publish(message);
}

void MediaCaptureNode::Stop() { session_->Stop(); }
}  // namespace raven::media
