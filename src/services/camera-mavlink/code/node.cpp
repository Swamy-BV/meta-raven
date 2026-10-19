// SPDX-License-Identifier: AGPL-3.0-only

#include "node.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <rclcpp/qos.hpp>
#include <rclcpp/time.hpp>
#include <rclcpp_action/create_client.hpp>

namespace raven::camera {
using OutputClient = rclcpp::Client<raven_interfaces::srv::SetMediaOutput>;
using ModeClient = rclcpp::Client<raven_interfaces::srv::SetMediaMode>;

namespace {
constexpr auto kDefaultPort = 5760;
constexpr auto kMaximumId = 255;
constexpr auto kMinimumPort = 1024;
constexpr auto kMaximumPort = 65535;
constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kStateTimeout = std::chrono::seconds(3);
constexpr auto kOperationTimeout = std::chrono::seconds(10);
constexpr auto kRetryWindow = std::chrono::seconds(5);
constexpr auto kEventHistory = 10;
constexpr auto kRecentLimit = 32U;
constexpr auto kMaximumPhotos = 10000U;
constexpr auto kMinimumInterval = 0.1F;
constexpr auto kMaximumInterval = 3600.0F;
constexpr auto kMinimumTelemetryInterval = 100000.0F;
constexpr auto kMaximumTelemetryInterval = 60000000.0F;
constexpr auto kDefaultTelemetryInterval = 1000000.0F;
constexpr auto kCameraCount = 1.0F;

int ParameterId(rclcpp::Node& node, const std::string& name, int initial) {
  const auto value = node.declare_parameter<int>(name, initial);
  const auto maximum = name == "router_port" ? kMaximumPort : kMaximumId;
  const auto minimum = name == "router_port" ? kMinimumPort : 1;
  if (value < minimum || value > maximum) {
    throw std::invalid_argument("camera identity or router port out of bounds");
  }
  return static_cast<int>(value);
}

bool Integer(float value, std::uint32_t maximum) {
  return std::isfinite(value) && value >= 0 &&
         static_cast<double>(value) <= maximum && std::floor(value) == value;
}

bool SameRequest(const Command& left, const Command& right) {
  const auto& a = left.parameters;
  const auto& b = right.parameters;
  return left.system == right.system && left.component == right.component &&
         a.command == b.command && a.param1 == b.param1 &&
         a.param2 == b.param2 && a.param3 == b.param3 && a.param4 == b.param4 &&
         (a.param5 == b.param5 ||
          (std::isnan(a.param5) && std::isnan(b.param5))) &&
         (a.param6 == b.param6 ||
          (std::isnan(a.param6) && std::isnan(b.param6))) &&
         (a.param7 == b.param7 ||
          (std::isnan(a.param7) && std::isnan(b.param7)));
}

bool CameraTarget(const mavlink_command_long_t& command,
                  std::uint8_t component) {
  float camera = 0;
  switch (command.command) {
    case MAV_CMD_IMAGE_START_CAPTURE:
    case MAV_CMD_IMAGE_STOP_CAPTURE:
    case MAV_CMD_SET_CAMERA_MODE:
      camera = command.param1;
      break;
    case MAV_CMD_VIDEO_START_CAPTURE:
      camera = command.param3;
      break;
    case MAV_CMD_VIDEO_STOP_CAPTURE:
    case MAV_CMD_VIDEO_START_STREAMING:
    case MAV_CMD_VIDEO_STOP_STREAMING:
      camera = command.param2;
      break;
    default:
      break;
  }
  return camera == 0 || camera == static_cast<float>(component);
}

bool CameraMessage(float id) {
  if (!Integer(id, std::numeric_limits<std::uint32_t>::max())) {
    return false;
  }
  switch (static_cast<std::uint32_t>(id)) {
    case MAVLINK_MSG_ID_CAMERA_INFORMATION:
    case MAVLINK_MSG_ID_CAMERA_SETTINGS:
    case MAVLINK_MSG_ID_STORAGE_INFORMATION:
    case MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS:
    case MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION:
    case MAVLINK_MSG_ID_VIDEO_STREAM_STATUS:
      return true;
    default:
      return false;
  }
}

bool CameraCommand(const mavlink_command_long_t& command) {
  if (command.command == MAV_CMD_REQUEST_MESSAGE ||
      command.command == MAV_CMD_SET_MESSAGE_INTERVAL) {
    return CameraMessage(command.param1);
  }
  return (command.command >= MAV_CMD_REQUEST_CAMERA_INFORMATION &&
          command.command <= MAV_CMD_SET_CAMERA_FOCUS) ||
         command.command == MAV_CMD_IMAGE_START_CAPTURE ||
         command.command == MAV_CMD_IMAGE_STOP_CAPTURE ||
         (command.command >= MAV_CMD_VIDEO_START_CAPTURE &&
          command.command <= MAV_CMD_REQUEST_VIDEO_STREAM_STATUS);
}
}  // namespace

CameraNode::CameraNode()
    : Node("camera_mavlink"),
      system_(static_cast<std::uint8_t>(ParameterId(*this, "system_id", 1))),
      component_(static_cast<std::uint8_t>(
          ParameterId(*this, "component_id", MAV_COMP_ID_CAMERA))),
      link_({system_, component_,
             static_cast<std::uint16_t>(
                 ParameterId(*this, "router_port", kDefaultPort))}),
      camera_namespace_(declare_parameter<std::string>(
          "camera_namespace", "/raven/vehicle_1/cameras/front")),
      stream_uri_(declare_parameter<std::string>("stream_uri", "")),
      started_(std::chrono::steady_clock::now()) {
  const auto qos = rclcpp::QoS(1).reliable().transient_local();
  state_subscription_ = create_subscription<MediaState>(
      camera_namespace_ + "/state", qos,
      [this](const MediaState::ConstSharedPtr& state) {
        if (state->recording && !state_.recording) {
          recording_started_ = std::chrono::steady_clock::now();
        }
        state_ = *state;
        have_state_ = true;
        state_received_ = std::chrono::steady_clock::now();
      });
  capabilities_subscription_ = create_subscription<Capabilities>(
      camera_namespace_ + "/capabilities", qos,
      [this](const Capabilities::ConstSharedPtr& capabilities) {
        capabilities_ = *capabilities;
      });
  events_ = create_subscription<raven_interfaces::msg::MediaCaptureEvent>(
      camera_namespace_ + "/capture_events",
      rclcpp::QoS(kEventHistory).reliable(),
      [this](const raven_interfaces::msg::MediaCaptureEvent::ConstSharedPtr&
                 event) { Captured(*event); });
  mode_ = create_client<raven_interfaces::srv::SetMediaMode>(camera_namespace_ +
                                                             "/set_mode");
  output_ = create_client<raven_interfaces::srv::SetMediaOutput>(
      camera_namespace_ + "/set_output");
  capture_ = rclcpp_action::create_client<Capture>(
      this, camera_namespace_ + "/capture_photos");
  intervals_[MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS] = std::chrono::seconds(1);
  poll_ = create_wall_timer(kPollInterval, [this] { Tick(); });
  heartbeat_ =
      create_wall_timer(std::chrono::seconds(1), [this] { Heartbeat(); });
}

bool CameraNode::Fresh() const {
  return have_state_ && state_.ready &&
         std::chrono::steady_clock::now() - state_received_ < kStateTimeout;
}

std::uint32_t CameraNode::BootMilliseconds() const {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started_)
          .count());
}

void CameraNode::Tick() {
  link_.Tick([this](const mavlink_message_t& message) { Receive(message); });
  const auto now = std::chrono::steady_clock::now();
  if (pending_ && now >= deadline_) {
    Finish(*pending_, MAV_RESULT_FAILED);
    output_->prune_pending_requests();
    mode_->prune_pending_requests();
  }
  for (const auto& [id, interval] : intervals_) {
    if (interval.count() > 0 && now >= next_publication_[id]) {
      static_cast<void>(Publish(id));
      next_publication_[id] = now + interval;
    }
  }
}

void CameraNode::Heartbeat() {
  mavlink_heartbeat_t packet{};
  packet.type = MAV_TYPE_CAMERA;
  packet.autopilot = MAV_AUTOPILOT_INVALID;
  packet.system_status = Fresh() ? MAV_STATE_ACTIVE : MAV_STATE_CRITICAL;
  packet.mavlink_version = 3;
  link_.Send(packet, mavlink_msg_heartbeat_encode_status);
}

void CameraNode::Acknowledge(const Command& command, std::uint8_t result) {
  mavlink_command_ack_t packet{};
  packet.command = command.parameters.command;
  packet.result = result;
  packet.target_system = command.system;
  packet.target_component = command.component;
  link_.Send(packet, mavlink_msg_command_ack_encode_status);
}

void CameraNode::Finish(const Command& command, std::uint8_t result) {
  if (!pending_ || command.token != pending_->token) {
    return;
  }
  // Copy before resetting: timeout completion may pass the pending value
  // itself.
  const auto completed = command;
  pending_.reset();
  Acknowledge(completed, result);
  recent_.push_back(
      {completed, result, std::chrono::steady_clock::now() + kRetryWindow});
  if (recent_.size() > kRecentLimit) {
    recent_.pop_front();
  }
}

bool CameraNode::Repeated(const Command& command) {
  const auto now = std::chrono::steady_clock::now();
  while (!recent_.empty() && recent_.front().expires < now) {
    recent_.pop_front();
  }
  if (pending_ && SameRequest(*pending_, command)) {
    Acknowledge(command, MAV_RESULT_IN_PROGRESS);
    return true;
  }
  const bool sequenced_photo =
      command.parameters.command == MAV_CMD_IMAGE_START_CAPTURE &&
      command.parameters.param3 == 1 && command.parameters.param4 != 0;
  if (command.parameters.confirmation == 0 && !sequenced_photo) {
    return false;
  }
  for (const auto& previous : recent_) {
    if (SameRequest(previous.command, command)) {
      Acknowledge(command, previous.result);
      return true;
    }
  }
  return false;
}

void CameraNode::Receive(const mavlink_message_t& message) {
  if (message.msgid != MAVLINK_MSG_ID_COMMAND_LONG) {
    return;
  }
  Command command;
  mavlink_msg_command_long_decode(&message, &command.parameters);
  const auto& parameters = command.parameters;
  if ((parameters.target_system != 0 && parameters.target_system != system_) ||
      (parameters.target_component != 0 &&
       parameters.target_component != component_)) {
    return;
  }
  command.system = message.sysid;
  if (!CameraTarget(parameters, component_) ||
      (parameters.target_component == 0 && !CameraCommand(parameters))) {
    return;
  }
  command.component = message.compid;
  command.token = ++next_token_;
  if (Repeated(command)) {
    return;
  }
  if (pending_) {
    Acknowledge(command, MAV_RESULT_TEMPORARILY_REJECTED);
    return;
  }
  pending_ = command;
  deadline_ = std::chrono::steady_clock::now() + kOperationTimeout;
  Dispatch(command);
}

bool CameraNode::InformationRequest(const Command& command) {
  switch (command.parameters.command) {
    case MAV_CMD_REQUEST_MESSAGE:
      RequestMessage(command);
      return true;
    case MAV_CMD_SET_MESSAGE_INTERVAL:
      SetInterval(command);
      return true;
    default:
      break;
  }
  const std::map<std::uint16_t, std::uint32_t> requests{
      {MAV_CMD_REQUEST_CAMERA_INFORMATION, MAVLINK_MSG_ID_CAMERA_INFORMATION},
      {MAV_CMD_REQUEST_CAMERA_SETTINGS, MAVLINK_MSG_ID_CAMERA_SETTINGS},
      {MAV_CMD_REQUEST_STORAGE_INFORMATION, MAVLINK_MSG_ID_STORAGE_INFORMATION},
      {MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS,
       MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS},
      {MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION,
       MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION},
      {MAV_CMD_REQUEST_VIDEO_STREAM_STATUS,
       MAVLINK_MSG_ID_VIDEO_STREAM_STATUS}};
  const auto found = requests.find(command.parameters.command);
  if (found == requests.end()) {
    return false;
  }
  auto request = command;
  request.parameters.param1 = static_cast<float>(found->second);
  RequestMessage(request);
  return true;
}

void CameraNode::Dispatch(const Command& command) {
  if (InformationRequest(command)) {
    return;
  }
  if (!Fresh()) {
    Finish(command, MAV_RESULT_TEMPORARILY_REJECTED);
    return;
  }
  switch (command.parameters.command) {
    case MAV_CMD_IMAGE_START_CAPTURE:
      StartPhotos(command);
      return;
    case MAV_CMD_IMAGE_STOP_CAPTURE:
      StopPhotos(command);
      return;
    case MAV_CMD_VIDEO_START_CAPTURE:
      SetOutput(command, "recording", true);
      return;
    case MAV_CMD_VIDEO_STOP_CAPTURE:
      SetOutput(command, "recording", false);
      return;
    case MAV_CMD_VIDEO_START_STREAMING:
      SetOutput(command, "streaming", true);
      return;
    case MAV_CMD_VIDEO_STOP_STREAMING:
      SetOutput(command, "streaming", false);
      return;
    case MAV_CMD_SET_CAMERA_MODE:
      SetMode(command);
      return;
    default:
      Finish(command, MAV_RESULT_UNSUPPORTED);
      return;
  }
}

void CameraNode::SetOutput(const Command& command, const std::string& output,
                           bool enabled) {
  if (!Integer(command.parameters.param1, 1) || !output_->service_is_ready()) {
    Finish(command, MAV_RESULT_TEMPORARILY_REJECTED);
    return;
  }
  auto request =
      std::make_shared<raven_interfaces::srv::SetMediaOutput::Request>();
  request->output = output;
  request->enabled = enabled;
  const OutputClient::CallbackType callback =
      [this, command](const OutputClient::SharedFuture& response) {
        const auto& result = response.get();
        Finish(command,
               result->success ? MAV_RESULT_ACCEPTED : MAV_RESULT_FAILED);
      };
  output_->async_send_request(request, callback);
}

void CameraNode::SetMode(const Command& command) {
  const auto mode = command.parameters.param2;
  if (!Integer(mode, 1) || !mode_->service_is_ready()) {
    Finish(command, MAV_RESULT_DENIED);
    return;
  }
  auto request =
      std::make_shared<raven_interfaces::srv::SetMediaMode::Request>();
  request->mode = mode == CAMERA_MODE_IMAGE ? "photo" : "video";
  const ModeClient::CallbackType callback =
      [this, command](const ModeClient::SharedFuture& response) {
        const auto& result = response.get();
        Finish(command,
               result->success ? MAV_RESULT_ACCEPTED : MAV_RESULT_FAILED);
      };
  mode_->async_send_request(request, callback);
}

void CameraNode::StartPhotos(const Command& command) {
  const auto& parameters = command.parameters;
  if (!Integer(parameters.param3, kMaximumPhotos) ||
      !std::isfinite(parameters.param2) || parameters.param2 < 0 ||
      parameters.param2 > kMaximumInterval ||
      (parameters.param3 != kCameraCount &&
       parameters.param2 < kMinimumInterval)) {
    Finish(command, MAV_RESULT_DENIED);
    return;
  }
  if (capture_goal_ || state_.capturing ||
      !capture_->action_server_is_ready()) {
    Finish(command, MAV_RESULT_TEMPORARILY_REJECTED);
    return;
  }
  Capture::Goal goal;
  goal.request_id = "mav-" + std::to_string(command.system) + "-" +
                    std::to_string(command.token);
  goal.count = static_cast<std::uint32_t>(parameters.param3);
  goal.interval = parameters.param2;
  auto options = rclcpp_action::Client<Capture>::SendGoalOptions{};
  options.goal_response_callback =
      [this, command](const CaptureGoal::SharedPtr& handle) {
        PhotoAccepted(command, handle);
      };
  options.result_callback = [this](const CaptureGoal::WrappedResult& result) {
    PhotoResult(result);
  };
  capture_->async_send_goal(goal, options);
}

void CameraNode::PhotoAccepted(const Command& command,
                               const CaptureGoal::SharedPtr& handle) {
  if (!pending_ || pending_->token != command.token) {
    if (handle) {
      capture_->async_cancel_goal(handle);
    }
    return;
  }
  capture_goal_ = handle;
  photo_interval_ = command.parameters.param2;
  Finish(command,
         handle ? MAV_RESULT_ACCEPTED : MAV_RESULT_TEMPORARILY_REJECTED);
}

void CameraNode::PhotoResult(const CaptureGoal::WrappedResult& result) {
  if (!capture_goal_) {
    return;
  }
  const auto& id = capture_goal_->get_goal_id();
  if (!std::equal(id.begin(), id.end(), result.goal_id.begin())) {
    return;
  }
  capture_goal_.reset();
  photo_interval_ = 0;
  if (result.code != rclcpp_action::ResultCode::SUCCEEDED &&
      result.code != rclcpp_action::ResultCode::CANCELED) {
    raven_interfaces::msg::MediaCaptureEvent failure;
    failure.header.stamp = now();
    failure.success = false;
    Captured(failure);
  }
}

void CameraNode::StopPhotos(const Command& command) {
  if (!capture_goal_) {
    Finish(command, state_.capturing ? MAV_RESULT_TEMPORARILY_REJECTED
                                     : MAV_RESULT_ACCEPTED);
    return;
  }
  capture_->async_cancel_goal(
      capture_goal_, [this, command](const auto& response) {
        Finish(command, response->return_code == 0 ? MAV_RESULT_ACCEPTED
                                                   : MAV_RESULT_FAILED);
      });
}

void CameraNode::RequestMessage(const Command& command) {
  const auto id = command.parameters.param1;
  if (!Integer(id, std::numeric_limits<std::uint32_t>::max())) {
    Finish(command, MAV_RESULT_DENIED);
    return;
  }
  Finish(command, Publish(static_cast<std::uint32_t>(id))
                      ? MAV_RESULT_ACCEPTED
                      : MAV_RESULT_UNSUPPORTED);
}

void CameraNode::SetInterval(const Command& command) {
  const auto id = command.parameters.param1;
  auto interval = command.parameters.param2;
  if (!Integer(id, std::numeric_limits<std::uint32_t>::max()) ||
      !std::isfinite(interval) ||
      (interval != -1 && interval != 0 &&
       (interval < kMinimumTelemetryInterval ||
        interval > kMaximumTelemetryInterval))) {
    Finish(command, MAV_RESULT_DENIED);
    return;
  }
  const auto message_id = static_cast<std::uint32_t>(id);
  if (message_id != MAVLINK_MSG_ID_CAMERA_CAPTURE_STATUS &&
      message_id != MAVLINK_MSG_ID_VIDEO_STREAM_STATUS &&
      message_id != MAVLINK_MSG_ID_CAMERA_SETTINGS) {
    Finish(command, MAV_RESULT_UNSUPPORTED);
    return;
  }
  if (interval == 0) {
    interval = kDefaultTelemetryInterval;
  }
  intervals_[message_id] =
      std::chrono::microseconds(static_cast<std::int64_t>(interval));
  Finish(command, MAV_RESULT_ACCEPTED);
}
}  // namespace raven::camera
