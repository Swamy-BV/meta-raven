// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
// GCC versions expose variant access through different STL headers.
#include <utility>  // IWYU pragma: keep
#include <variant>

#include "raven/contracts/common.h"

namespace raven::contracts {

enum class Validity : std::int32_t { kUnknown, kInvalid, kValid };
enum class ArmedState : std::int32_t { kUnknown, kDisarmed, kArmed };
enum class FlightMode : std::int32_t {
  kUnknown,
  kManual,
  kHold,
  kTakeoff,
  kLand,
  kMission,
  kOffboard,
  kReturn
};

struct PositionState {
  FrameId enu_frame{};
  Vector3 position_m{};
  Validity validity = Validity::kUnknown;
  std::optional<double> uncertainty_m;
  SampleTime sample{};
};

struct VelocityState {
  FrameId enu_frame{};
  Vector3 velocity_mps{};
  Validity validity = Validity::kUnknown;
  std::optional<double> uncertainty_mps;
  SampleTime sample{};
};

struct BatteryState {
  double remaining_fraction = 0.0;
  Validity validity = Validity::kUnknown;
};

struct VehicleState {
  VehicleId vehicle_id{};
  SessionId session_id{};
  SampleTime sample{};
  ArmedState armed = ArmedState::kUnknown;
  FlightMode mode = FlightMode::kUnknown;
  std::optional<PositionState> position;
  std::optional<VelocityState> velocity;
  std::optional<Quaternion> attitude;
  std::optional<BatteryState> battery;
};

inline bool IsKnownValidity(Validity validity) {
  return validity == Validity::kUnknown || validity == Validity::kInvalid ||
         validity == Validity::kValid;
}

[[nodiscard]] inline ValidationResult ValidatePosition(
    const PositionState& position, const ValidationContext& context,
    std::uint64_t max_age_us) {
  if (position.validity == Validity::kValid) {
    const auto freshness =
        ValidateFreshness(position.sample, context, max_age_us);
    if (!freshness.Ok()) {
      return freshness;
    }
  }
  if (!position.enu_frame.IsValid() || !IsFinite(position.position_m) ||
      !IsKnownValidity(position.validity) ||
      (position.uncertainty_m && (!std::isfinite(*position.uncertainty_m) ||
                                  *position.uncertainty_m < 0.0))) {
    return {ErrorCode::kInvalidArgument};
  }
  return {};
}

[[nodiscard]] inline ValidationResult ValidateVelocity(
    const VelocityState& velocity, const ValidationContext& context,
    std::uint64_t max_age_us) {
  if (velocity.validity == Validity::kValid) {
    const auto freshness =
        ValidateFreshness(velocity.sample, context, max_age_us);
    if (!freshness.Ok()) {
      return freshness;
    }
  }
  if (!velocity.enu_frame.IsValid() || !IsFinite(velocity.velocity_mps) ||
      !IsKnownValidity(velocity.validity) ||
      (velocity.uncertainty_mps && (!std::isfinite(*velocity.uncertainty_mps) ||
                                    *velocity.uncertainty_mps < 0.0))) {
    return {ErrorCode::kInvalidArgument};
  }
  return {};
}

inline bool IsValidBattery(const BatteryState& battery) {
  return IsKnownValidity(battery.validity) &&
         std::isfinite(battery.remaining_fraction) &&
         battery.remaining_fraction >= 0.0 && battery.remaining_fraction <= 1.0;
}

[[nodiscard]] inline ValidationResult ValidateVehicleState(
    const VehicleState& state, const ValidationContext& context,
    std::uint64_t max_age_us) {
  if (state.vehicle_id != context.vehicle_id ||
      state.session_id != context.session_id) {
    return {ErrorCode::kConflict};
  }
  const auto freshness = ValidateFreshness(state.sample, context, max_age_us);
  if (!freshness.Ok()) {
    return freshness;
  }
  if ((state.armed != ArmedState::kUnknown &&
       state.armed != ArmedState::kDisarmed &&
       state.armed != ArmedState::kArmed) ||
      state.mode < FlightMode::kUnknown || state.mode > FlightMode::kReturn) {
    return {ErrorCode::kInvalidArgument};
  }
  const auto position =
      state.position ? ValidatePosition(*state.position, context, max_age_us)
                     : ValidationResult{};
  if (!position.Ok()) {
    return position;
  }
  const auto velocity =
      state.velocity ? ValidateVelocity(*state.velocity, context, max_age_us)
                     : ValidationResult{};
  if (!velocity.Ok()) {
    return velocity;
  }
  if ((state.attitude && !IsUnitQuaternion(*state.attitude)) ||
      (state.battery && !IsValidBattery(*state.battery))) {
    return {ErrorCode::kInvalidArgument};
  }
  return {};
}
enum class Permission : std::uint32_t {
  kTakeoff = 1U << 0U,
  kHold = 1U << 1U,
  kGoTo = 1U << 2U,
  kReturn = 1U << 3U,
  kLand = 1U << 4U,
  kPositionReference = 1U << 5U,
  kVelocityReference = 1U << 6U
};

inline constexpr std::uint32_t Mask(Permission permission) {
  return static_cast<std::uint32_t>(permission);
}

inline constexpr std::uint32_t kKnownPermissions = (1U << 7U) - 1U;

struct ControlLease {
  VehicleId vehicle_id{};
  SessionId session_id{};
  CallerId owner{};
  LeaseId lease_id{};
  std::uint32_t permissions = 0;
  std::uint64_t issued_at_us = 0;
  Deadline expiry{};
};

struct ControlContext {
  ValidationContext target{};
  CallerId authenticated_caller{};
};

[[nodiscard]] inline ValidationResult ValidateLease(
    const ControlLease& lease, const ControlContext& context,
    Permission required) {
  const std::uint32_t required_mask = Mask(required);
  if (required_mask == 0 || (required_mask & (required_mask - 1)) != 0 ||
      (required_mask & ~kKnownPermissions) != 0 ||
      !IsValidContext(context.target) ||
      !context.authenticated_caller.IsValid() || !lease.lease_id.IsValid() ||
      !lease.owner.IsValid() || lease.permissions == 0 ||
      (lease.permissions & ~kKnownPermissions) != 0 ||
      lease.expiry.expires_at_us <= lease.issued_at_us ||
      lease.issued_at_us > context.target.now_us) {
    return {ErrorCode::kInvalidArgument};
  }
  if (lease.vehicle_id != context.target.vehicle_id ||
      lease.session_id != context.target.session_id) {
    return {ErrorCode::kConflict};
  }
  if (lease.owner != context.authenticated_caller ||
      (lease.permissions & Mask(required)) == 0 ||
      (Mask(required) & ~kKnownPermissions) != 0) {
    return {ErrorCode::kPermissionDenied};
  }
  if (lease.expiry.expires_at_us <= context.target.now_us) {
    return {ErrorCode::kLeaseExpired};
  }
  return {};
}

// Explicit ENU operating volume; no implicit origin or permissive defaults.
struct FlightLimits {
  FrameId enu_frame{};
  Vector3 minimum_m{};
  Vector3 maximum_m{};
  double max_takeoff_height_m = 0.0;
  double max_speed_mps = 0.0;
  double max_yaw_rate_radps = 0.0;
  std::uint64_t max_state_age_us = 0;
  std::uint64_t max_reference_age_us = 0;
  std::uint64_t max_admission_ttl_us = 0;
  std::uint64_t max_execution_timeout_us = 0;
};

inline bool IsValidLimits(const FlightLimits& limits) {
  return limits.enu_frame.IsValid() && IsFinite(limits.minimum_m) &&
         IsFinite(limits.maximum_m) &&
         limits.minimum_m.x < limits.maximum_m.x &&
         limits.minimum_m.y < limits.maximum_m.y &&
         limits.minimum_m.z < limits.maximum_m.z &&
         std::isfinite(limits.max_takeoff_height_m) &&
         limits.max_takeoff_height_m > 0.0 &&
         std::isfinite(limits.max_speed_mps) && limits.max_speed_mps > 0.0 &&
         std::isfinite(limits.max_yaw_rate_radps) &&
         limits.max_yaw_rate_radps > 0.0 && limits.max_state_age_us > 0 &&
         limits.max_reference_age_us > 0 && limits.max_admission_ttl_us > 0 &&
         limits.max_execution_timeout_us > 0;
}

inline bool IsInside(const Vector3& position, const FlightLimits& limits) {
  return IsFinite(position) && position.x >= limits.minimum_m.x &&
         position.x <= limits.maximum_m.x && position.y >= limits.minimum_m.y &&
         position.y <= limits.maximum_m.y && position.z >= limits.minimum_m.z &&
         position.z <= limits.maximum_m.z;
}

inline bool HasPosition(const VehicleState& state, const FlightLimits& limits) {
  return state.position && state.position->validity == Validity::kValid &&
         state.position->enu_frame == limits.enu_frame;
}

inline constexpr double kPi = 3.14159265358979323846;

inline bool IsValidYaw(double yaw_rad) {
  return std::isfinite(yaw_rad) && yaw_rad >= -kPi && yaw_rad <= kPi;
}

struct TakeoffArguments {
  double height_m = 0.0;

  bool operator==(const TakeoffArguments& other) const {
    return height_m == other.height_m;
  }
};

struct HoldArguments {
  bool operator==(const HoldArguments&) const { return true; }
};

struct GoToArguments {
  FrameId enu_frame{};
  Vector3 position_m{};
  double yaw_rad = 0.0;

  bool operator==(const GoToArguments& other) const {
    return enu_frame == other.enu_frame && position_m == other.position_m &&
           yaw_rad == other.yaw_rad;
  }
};

struct ReturnArguments {
  bool operator==(const ReturnArguments&) const { return true; }
};

struct LandArguments {
  bool operator==(const LandArguments&) const { return true; }
};

using FlightArguments =
    std::variant<TakeoffArguments, HoldArguments, GoToArguments,
                 ReturnArguments, LandArguments>;

struct FlightRequest {
  SchemaVersion schema{};
  VehicleId vehicle_id{};
  SessionId session_id{};
  CallerId caller_id{};
  CommandId command_id{};
  LeaseId lease_id{};
  Deadline admission_deadline{};
  std::uint64_t execution_timeout_us = 0;
  FlightArguments arguments = HoldArguments{};
};

inline Permission RequiredPermission(const FlightArguments& arguments) {
  if (std::holds_alternative<TakeoffArguments>(arguments)) {
    return Permission::kTakeoff;
  }
  if (std::holds_alternative<HoldArguments>(arguments)) {
    return Permission::kHold;
  }
  if (std::holds_alternative<GoToArguments>(arguments)) {
    return Permission::kGoTo;
  }
  if (std::holds_alternative<ReturnArguments>(arguments)) {
    return Permission::kReturn;
  }
  return Permission::kLand;
}

[[nodiscard]] inline ValidationResult ValidateRequestEnvelope(
    const FlightRequest& request, const ControlLease& lease,
    const ControlContext& context, const FlightLimits& limits) {
  const auto version = ValidateVersion(request.schema);
  if (!version.Ok()) {
    return version;
  }
  if (!request.command_id.IsValid() || !IsValidLimits(limits) ||
      request.execution_timeout_us == 0 ||
      request.execution_timeout_us > limits.max_execution_timeout_us) {
    return {ErrorCode::kInvalidArgument};
  }
  if (request.vehicle_id != context.target.vehicle_id ||
      request.session_id != context.target.session_id) {
    return {ErrorCode::kConflict};
  }
  if (request.caller_id != context.authenticated_caller ||
      request.lease_id != lease.lease_id) {
    return {ErrorCode::kPermissionDenied};
  }
  const auto deadline = ValidateDeadline(
      request.admission_deadline, context.target, limits.max_admission_ttl_us);
  if (!deadline.Ok()) {
    return deadline;
  }
  const auto authority =
      ValidateLease(lease, context, RequiredPermission(request.arguments));
  if (!authority.Ok()) {
    return authority;
  }
  return {};
}

[[nodiscard]] inline ValidationResult ValidateFlightArguments(
    const FlightArguments& arguments, const VehicleState& state,
    const FlightLimits& limits) {
  if (const auto* takeoff = std::get_if<TakeoffArguments>(&arguments)) {
    if (!std::isfinite(takeoff->height_m) || takeoff->height_m <= 0.0 ||
        takeoff->height_m > limits.max_takeoff_height_m) {
      return {ErrorCode::kInvalidArgument};
    }
    return {state.armed == ArmedState::kDisarmed && HasPosition(state, limits)
                ? ErrorCode::kNone
                : ErrorCode::kNotReady};
  }
  if (const auto* go_to = std::get_if<GoToArguments>(&arguments)) {
    if (go_to->enu_frame != limits.enu_frame ||
        !IsInside(go_to->position_m, limits) || !IsValidYaw(go_to->yaw_rad)) {
      return {ErrorCode::kInvalidArgument};
    }
  }
  if (state.armed != ArmedState::kArmed) {
    return {ErrorCode::kNotReady};
  }
  if (!std::holds_alternative<LandArguments>(arguments) &&
      !HasPosition(state, limits)) {
    return {ErrorCode::kNotReady};
  }
  return {};
}

[[nodiscard]] inline ValidationResult ValidateFlightRequest(
    const FlightRequest& request, const ControlLease& lease,
    const ControlContext& context, const VehicleState& state,
    const FlightLimits& limits) {
  const auto envelope =
      ValidateRequestEnvelope(request, lease, context, limits);
  if (!envelope.Ok()) {
    return envelope;
  }
  const auto telemetry =
      ValidateVehicleState(state, context.target, limits.max_state_age_us);
  if (!telemetry.Ok()) {
    return telemetry;
  }
  if (state.mode == FlightMode::kUnknown) {
    return {ErrorCode::kNotReady};
  }
  return ValidateFlightArguments(request.arguments, state, limits);
}

// Compare only validated same-session requests; expiry cannot be extended by
// retry.
[[nodiscard]] inline ValidationResult CheckRetry(const FlightRequest& original,
                                                 const FlightRequest& retry) {
  if (!original.vehicle_id.IsValid() || !original.session_id.IsValid() ||
      !original.caller_id.IsValid() || !original.command_id.IsValid() ||
      !original.lease_id.IsValid()) {
    return {ErrorCode::kInvalidArgument};
  }
  if (original.vehicle_id != retry.vehicle_id ||
      original.session_id != retry.session_id ||
      original.caller_id != retry.caller_id ||
      original.command_id != retry.command_id ||
      original.lease_id != retry.lease_id ||
      original.schema.major != retry.schema.major ||
      original.schema.minor != retry.schema.minor ||
      original.admission_deadline.expires_at_us !=
          retry.admission_deadline.expires_at_us ||
      original.execution_timeout_us != retry.execution_timeout_us ||
      !(original.arguments == retry.arguments)) {
    return {ErrorCode::kConflict};
  }
  return {};
}

struct PositionReference {
  Vector3 position_m{};
  double yaw_rad = 0.0;
};

struct VelocityReference {
  Vector3 velocity_mps{};
  double yaw_rate_radps = 0.0;
};

struct MotionReference {
  SchemaVersion schema{};
  VehicleId vehicle_id{};
  SessionId session_id{};
  CallerId caller_id{};
  LeaseId lease_id{};
  FrameId enu_frame{};
  SampleTime sample{};
  std::uint64_t sequence = 0;
  Deadline expiry{};
  std::variant<PositionReference, VelocityReference> target =
      PositionReference{};
};

[[nodiscard]] inline ValidationResult ValidateReferenceEnvelope(
    const MotionReference& reference, const ControlLease& lease,
    const ControlContext& context, const FlightLimits& limits,
    std::optional<std::uint64_t> last_sequence) {
  const auto version = ValidateVersion(reference.schema);
  if (!version.Ok()) {
    return version;
  }
  if (!IsValidLimits(limits) || reference.sequence == 0) {
    return {ErrorCode::kInvalidArgument};
  }
  if (reference.vehicle_id != context.target.vehicle_id ||
      reference.session_id != context.target.session_id ||
      (last_sequence && reference.sequence <= *last_sequence)) {
    return {ErrorCode::kConflict};
  }
  if (reference.caller_id != context.authenticated_caller ||
      reference.lease_id != lease.lease_id) {
    return {ErrorCode::kPermissionDenied};
  }
  if (reference.enu_frame != limits.enu_frame) {
    return {ErrorCode::kInvalidArgument};
  }
  const auto freshness = ValidateFreshness(reference.sample, context.target,
                                           limits.max_reference_age_us);
  if (!freshness.Ok()) {
    return freshness;
  }
  const auto expiry = ValidateDeadline(reference.expiry, context.target,
                                       limits.max_reference_age_us);
  if (!expiry.Ok()) {
    return expiry;
  }
  const bool position_target =
      std::holds_alternative<PositionReference>(reference.target);
  const auto authority =
      ValidateLease(lease, context,
                    position_target ? Permission::kPositionReference
                                    : Permission::kVelocityReference);
  if (!authority.Ok()) {
    return authority;
  }
  return {};
}

struct MotionValidationContext {
  ControlContext control{};
  std::optional<std::uint64_t> last_sequence;
};

[[nodiscard]] inline ValidationResult ValidateMotionReference(
    const MotionReference& reference, const ControlLease& lease,
    const MotionValidationContext& motion, const VehicleState& state,
    const FlightLimits& limits) {
  const auto& context = motion.control;
  const auto envelope = ValidateReferenceEnvelope(reference, lease, context,
                                                  limits, motion.last_sequence);
  if (!envelope.Ok()) {
    return envelope;
  }
  const auto telemetry =
      ValidateVehicleState(state, context.target, limits.max_state_age_us);
  if (!telemetry.Ok()) {
    return telemetry;
  }
  if (state.armed != ArmedState::kArmed ||
      state.mode != FlightMode::kOffboard || !HasPosition(state, limits)) {
    return {ErrorCode::kNotReady};
  }
  if (const auto* position =
          std::get_if<PositionReference>(&reference.target)) {
    if (!IsInside(position->position_m, limits) ||
        !IsValidYaw(position->yaw_rad)) {
      return {ErrorCode::kInvalidArgument};
    }
  } else if (const auto* velocity =
                 std::get_if<VelocityReference>(&reference.target)) {
    if (!state.velocity || state.velocity->validity != Validity::kValid ||
        state.velocity->enu_frame != limits.enu_frame) {
      return {ErrorCode::kNotReady};
    }
    if (!IsFinite(velocity->velocity_mps) ||
        std::hypot(velocity->velocity_mps.x, velocity->velocity_mps.y,
                   velocity->velocity_mps.z) > limits.max_speed_mps ||
        !std::isfinite(velocity->yaw_rate_radps) ||
        std::abs(velocity->yaw_rate_radps) > limits.max_yaw_rate_radps) {
      return {ErrorCode::kInvalidArgument};
    }
  }
  return {};
}
enum class CommandState : std::int32_t {
  kReceived,
  kRejected,
  kAccepted,
  kDispatched,
  kAcknowledged,
  kSucceeded,
  kFailed,
  kCancelled,
  kOutcomeUnknown
};

// State transition direction matters.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] inline bool CanTransition(CommandState from, CommandState to) {
  switch (from) {
    case CommandState::kReceived:
      return to == CommandState::kAccepted || to == CommandState::kRejected;
    case CommandState::kAccepted:
      return to == CommandState::kDispatched || to == CommandState::kFailed ||
             to == CommandState::kCancelled;
    case CommandState::kDispatched:
      return to == CommandState::kAcknowledged || to == CommandState::kFailed ||
             to == CommandState::kOutcomeUnknown;
    case CommandState::kAcknowledged:
      return to == CommandState::kSucceeded || to == CommandState::kFailed ||
             to == CommandState::kCancelled ||
             to == CommandState::kOutcomeUnknown;
    case CommandState::kOutcomeUnknown:
      return to == CommandState::kSucceeded || to == CommandState::kFailed;
    default:
      return false;
  }
}

struct CommandResult {
  VehicleId vehicle_id{};
  SessionId session_id{};
  CommandId command_id{};
  CommandState state = CommandState::kReceived;
  ErrorCode error = ErrorCode::kNone;
};

[[nodiscard]] inline ValidationResult ValidateCommandResult(
    const CommandResult& result, const CommandId& expected_command,
    const ValidationContext& context) {
  if (!IsValidContext(context) || !expected_command.IsValid() ||
      result.state < CommandState::kReceived ||
      result.state > CommandState::kOutcomeUnknown ||
      result.error < ErrorCode::kNone ||
      result.error > ErrorCode::kOutcomeUnknown) {
    return {ErrorCode::kInvalidArgument};
  }
  if (result.vehicle_id != context.vehicle_id ||
      result.session_id != context.session_id ||
      result.command_id != expected_command) {
    return {ErrorCode::kConflict};
  }
  switch (result.state) {
    case CommandState::kRejected:
    case CommandState::kFailed:
      return {result.error != ErrorCode::kNone ? ErrorCode::kNone
                                               : ErrorCode::kInvalidArgument};
    case CommandState::kCancelled:
      return {result.error == ErrorCode::kCancelled
                  ? ErrorCode::kNone
                  : ErrorCode::kInvalidArgument};
    case CommandState::kOutcomeUnknown:
      return {result.error == ErrorCode::kOutcomeUnknown
                  ? ErrorCode::kNone
                  : ErrorCode::kInvalidArgument};
    default:
      return {result.error == ErrorCode::kNone ? ErrorCode::kNone
                                               : ErrorCode::kInvalidArgument};
  }
}
}  // namespace raven::contracts
