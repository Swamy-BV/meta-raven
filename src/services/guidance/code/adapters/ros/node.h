// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/service.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>
#include <rclcpp_action/server.hpp>
#include <rclcpp_action/server_goal_handle.hpp>
#include <rclcpp_action/types.hpp>

#include "behaviors/follow.h"
#include "behaviors/go_to.h"
#include "behaviors/trail.h"
#include "core/types.h"
#include "estimation/motion.h"
#include "estimation/pose.h"
#include "execution/controller.h"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "planning/trajectory.h"
#include "px4_msgs/msg/offboard_control_mode.hpp"
#include "px4_msgs/msg/trajectory_setpoint.hpp"
#include "px4_msgs/msg/vehicle_attitude.hpp"
#include "px4_msgs/msg/vehicle_local_position.hpp"
#include "px4_msgs/msg/vehicle_status.hpp"
#include "raven_interfaces/action/go_to.hpp"
#include "raven_interfaces/srv/select_target.hpp"
#include "sensor_msgs/msg/camera_info.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "vision_msgs/msg/detection2_d.hpp"
#include "vision_msgs/msg/detection2_d_array.hpp"
#include "vision_msgs/msg/label_info.hpp"
#include "vision_msgs/msg/vision_info.hpp"

namespace raven::guidance {
// Default mutually-exclusive callbacks serialize selection, telemetry and
// control. This node never arms a vehicle or changes its flight mode.
class GuidanceNode final : public rclcpp::Node {
 public:
  GuidanceNode();

 private:
  struct Projection {
    std::optional<Vector> point;
    std::string reason;
  };

  using Clock = std::chrono::steady_clock;
  using Select = raven_interfaces::srv::SelectTarget;
  using Enable = std_srvs::srv::SetBool;
  using GoTo = raven_interfaces::action::GoTo;
  using GoalHandle = rclcpp_action::ServerGoalHandle<GoTo>;
  void ConnectGoTo();
  void ConnectTargetOutputs();
  void PublishPoint(
      const Vector& point,
      const rclcpp::Publisher<geometry_msgs::msg::PointStamped>::SharedPtr&
          publisher) const;
  [[nodiscard]] std::string PrepareGoTo(const GoTo::Goal& goal);
  rclcpp_action::GoalResponse HandleGoToGoal(
      const rclcpp_action::GoalUUID& uuid,
      const std::shared_ptr<const GoTo::Goal>& goal);
  void StartGoTo(const std::shared_ptr<GoalHandle>& goal);
  void UpdateGoTo(const Pose& pose);
  void FinishGoTo(bool success, const std::string& message);
  [[nodiscard]] bool ArmedOffboard() const;
  [[nodiscard]] Kinematics MeasuredMotion() const;
  [[nodiscard]] Kinematics InitialReference() const;
  [[nodiscard]] MotionLimits Limits() const;
  void ResetExecution();
  void StopOutput(const std::string& reason);
  void PublishTrajectory();
  [[nodiscard]] bool AdvanceControlClock();
  void Configure();
  void ValidateConfiguration() const;
  void Connect();
  void ReceiveTracks(const vision_msgs::msg::Detection2DArray& message);
  void ReceiveInfo(const sensor_msgs::msg::CameraInfo& message);
  void ReceiveLabels(const vision_msgs::msg::LabelInfo& message);
  [[nodiscard]] bool Supported(
      const vision_msgs::msg::Detection2D& detection) const;
  void WaitForTarget();
  void ReceiveMetadata(const vision_msgs::msg::VisionInfo& message);
  void ReceivePosition(const px4_msgs::msg::VehicleLocalPosition& message);
  void ReceiveAttitude(const px4_msgs::msg::VehicleAttitude& message);
  void SelectTarget(const Select::Request& request, Select::Response& response);
  [[nodiscard]] bool StartFollowing();
  void EnableFollowing(const Enable::Request& request,
                       Enable::Response& response);
  [[nodiscard]] bool FreshTelemetry() const;
  [[nodiscard]] bool FreshTracks() const;
  [[nodiscard]] bool FreshTarget() const;
  [[nodiscard]] Pose LatestPose() const;
  [[nodiscard]] Projection TargetPoint(
      const vision_msgs::msg::Detection2D& detection);
  void Tick();
  void UpdateCommand(const Pose& pose);
  void UpdateFollowingState();
  void UpdateBraking(const Pose& pose);
  void BeginBraking();
  void ResumeVelocity();
  void Hold(const std::string& reason);
  void PublishCommand();
  [[nodiscard]] px4_msgs::msg::TrajectorySetpoint MakeSetpoint(
      std::uint64_t timestamp) const;
  void TraceFrame(std::int64_t source_stamp_ns) const;
  void PublishHealth();
  void PublishObservationHealth(const std::string& rejection) const;

  Configuration config_;
  MotionExecutor execution_;
  std::optional<Kinematics> smooth_motion_;
  Trajectory trajectory_;
  Destination destination_;
  std::shared_ptr<GoalHandle> goal_;
  bool goal_reserved_ = false;
  bool action_stopping_ = false;
  bool full_velocity_ = false;
  bool allow_unmapped_motion_ = false;
  std::string local_frame_;
  std::string local_ned_frame_;
  double goal_started_ = 0;
  double control_step_ = 0;
  double planning_duration_ms_ = 0;
  double max_planning_duration_ms_ = 0;
  double braking_deadline_ = 0;
  rclcpp_action::Server<GoTo>::SharedPtr go_to_;
  Camera camera_;
  std::string px4_namespace_, tracks_topic_, info_topic_, metadata_topic_,
      labels_topic_;
  std::string selected_id_, selected_frame_, selected_class_, model_key_;
  std::string state_ = "idle";
  std::string reason_ = "Select a visible person or car";
  double timeout_seconds_ = 0;
  double loss_timeout_seconds_ = 0;
  std::int64_t target_stamp_ns_ = 0;
  std::int64_t model_stamp_ = 0;
  std::int64_t label_stamp_ = 0;
  std::set<std::string> supported_classes_;
  double max_displacement_ = 0;
  bool enabled_ = false;
  bool streaming_ = false;
  bool braking_ = false;
  std::optional<Vector> target_;
  Vector hold_position_{};
  Vector origin_{};
  Command command_;
  MotionEstimator motion_;
  TargetTrail trail_;
  std::int64_t last_control_stamp_ns_ = 0;
  std::optional<sensor_msgs::msg::CameraInfo> info_;
  std::optional<px4_msgs::msg::VehicleStatus> status_;
  std::optional<px4_msgs::msg::VehicleLocalPosition> position_;
  vision_msgs::msg::Detection2DArray tracks_;
  PositionSamples positions_;
  AttitudeSamples attitudes_;
  std::optional<AlignedPose> alignment_;
  std::uint64_t pose_alignment_failures_ = 0;
  Clock::time_point last_position_, last_attitude_, last_status_, last_target_;
  std::uint64_t setpoints_sent_ = 0;
  std::uint64_t observations_used_ = 0;
  std::uint64_t rejected_observations_ = 0;
  std::string last_rejection_;
  rclcpp::Subscription<vision_msgs::msg::Detection2DArray>::SharedPtr input_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr calibration_;
  rclcpp::Subscription<vision_msgs::msg::VisionInfo>::SharedPtr metadata_;
  rclcpp::Subscription<vision_msgs::msg::LabelInfo>::SharedPtr labels_;
  rclcpp::Subscription<px4_msgs::msg::VehicleStatus>::SharedPtr status_input_;
  rclcpp::Subscription<px4_msgs::msg::VehicleLocalPosition>::SharedPtr
      position_input_;
  rclcpp::Subscription<px4_msgs::msg::VehicleAttitude>::SharedPtr
      attitude_input_;
  rclcpp::Publisher<px4_msgs::msg::OffboardControlMode>::SharedPtr mode_;
  rclcpp::Publisher<px4_msgs::msg::TrajectorySetpoint>::SharedPtr setpoint_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
      observation_health_;
  rclcpp::Publisher<geometry_msgs::msg::PointStamped>::SharedPtr
      observation_output_,
      estimate_output_;
  rclcpp::Service<Select>::SharedPtr select_;
  rclcpp::Service<Enable>::SharedPtr enable_;
  rclcpp::TimerBase::SharedPtr control_timer_, health_timer_;
};
}  // namespace raven::guidance
