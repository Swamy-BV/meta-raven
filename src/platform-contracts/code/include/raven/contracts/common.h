// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace raven::contracts {
inline constexpr std::size_t kIdentifierCapacity = 64;
inline constexpr std::size_t kFrameIdentifierCapacity = 128;
inline constexpr double kQuaternionTolerance = 1e-6;

template <std::size_t Capacity, class Tag>
class FixedId {
 public:
  [[nodiscard]] static std::optional<FixedId> Parse(std::string_view text) {
    if (text.empty() || text.size() > Capacity) {
      return std::nullopt;
    }
    for (const char character : text) {
      if (character < '!' || character > '~') {
        return std::nullopt;
      }
    }
    FixedId result;
    std::copy(text.begin(), text.end(), result.bytes_.begin());
    result.length_ = text.size();
    return result;
  }

  [[nodiscard]] bool IsValid() const { return length_ != 0; }

  [[nodiscard]] std::string_view View() const {
    return {bytes_.data(), length_};
  }

  bool operator==(const FixedId& other) const { return View() == other.View(); }

  bool operator!=(const FixedId& other) const { return !(*this == other); }

 private:
  std::array<char, Capacity> bytes_{};
  std::size_t length_ = 0;
};

struct VehicleTag {};

struct SessionTag {};

struct CallerTag {};

struct CommandTag {};

struct LeaseTag {};

struct ClockTag {};

struct BootTag {};

struct FrameTag {};

using VehicleId = FixedId<kIdentifierCapacity, VehicleTag>;
using SessionId = FixedId<kIdentifierCapacity, SessionTag>;
using CallerId = FixedId<kIdentifierCapacity, CallerTag>;
using CommandId = FixedId<kIdentifierCapacity, CommandTag>;
using LeaseId = FixedId<kIdentifierCapacity, LeaseTag>;
using ClockId = FixedId<kIdentifierCapacity, ClockTag>;
using BootId = FixedId<kIdentifierCapacity, BootTag>;
using FrameId = FixedId<kFrameIdentifierCapacity, FrameTag>;

enum class ErrorCode : std::int32_t {
  kNone,
  kInvalidArgument,
  kUnsupportedVersion,
  kUnsupportedCapability,
  kPermissionDenied,
  kNotReady,
  kLeaseExpired,
  kStaleInput,
  kDeadlineExceeded,
  kConflict,
  kTransportUnavailable,
  kCancelled,
  kExecutionFailed,
  kOutcomeUnknown
};

struct ValidationResult {
  ErrorCode code = ErrorCode::kNone;

  [[nodiscard]] bool Ok() const { return code == ErrorCode::kNone; }
};

struct SchemaVersion {
  std::uint16_t major = 1;
  std::uint16_t minor = 0;
};

[[nodiscard]] inline ValidationResult ValidateVersion(
    const SchemaVersion& version) {
  return {version.major == 1 ? ErrorCode::kNone
                             : ErrorCode::kUnsupportedVersion};
}

struct ValidationContext {
  VehicleId vehicle_id{};
  SessionId session_id{};
  ClockId local_clock{};
  std::uint64_t now_us = 0;
};

inline bool IsValidContext(const ValidationContext& context) {
  return context.vehicle_id.IsValid() && context.session_id.IsValid() &&
         context.local_clock.IsValid();
}

// Adapter-normalized acquisition time. Unsynchronized samples fail closed.
struct SampleTime {
  ClockId local_clock{};
  BootId source_boot{};
  std::uint64_t captured_at_us = 0;
  std::uint64_t uncertainty_us = 0;
  bool synchronized = false;
};

[[nodiscard]] inline ValidationResult ValidateFreshness(
    const SampleTime& sample, const ValidationContext& context,
    std::uint64_t max_age_us) {
  if (!IsValidContext(context) || max_age_us == 0 ||
      !sample.source_boot.IsValid()) {
    return {ErrorCode::kInvalidArgument};
  }
  if (!sample.synchronized || sample.local_clock != context.local_clock ||
      sample.captured_at_us > context.now_us) {
    return {ErrorCode::kStaleInput};
  }
  const std::uint64_t age_us = context.now_us - sample.captured_at_us;
  if (sample.uncertainty_us > max_age_us ||
      age_us > max_age_us - sample.uncertainty_us) {
    return {ErrorCode::kStaleInput};
  }
  return {};
}

struct Deadline {
  std::uint64_t expires_at_us = 0;
};

[[nodiscard]] inline std::optional<Deadline> MakeDeadline(
    std::uint64_t now_us, std::uint64_t ttl_us, std::uint64_t max_ttl_us) {
  if (ttl_us == 0 || ttl_us > max_ttl_us ||
      ttl_us > std::numeric_limits<std::uint64_t>::max() - now_us) {
    return std::nullopt;
  }
  return Deadline{now_us + ttl_us};
}

[[nodiscard]] inline ValidationResult ValidateDeadline(
    const Deadline& deadline, const ValidationContext& context,
    std::uint64_t max_ttl_us) {
  if (!IsValidContext(context) || max_ttl_us == 0) {
    return {ErrorCode::kInvalidArgument};
  }
  if (deadline.expires_at_us <= context.now_us) {
    return {ErrorCode::kDeadlineExceeded};
  }
  if (deadline.expires_at_us - context.now_us > max_ttl_us) {
    return {ErrorCode::kInvalidArgument};
  }
  return {};
}

struct Vector3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  bool operator==(const Vector3& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

inline bool IsFinite(const Vector3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

struct Quaternion {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 1.0;
};

inline bool IsUnitQuaternion(const Quaternion& value) {
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      !std::isfinite(value.z) || !std::isfinite(value.w)) {
    return false;
  }
  const double norm =
      std::hypot(std::hypot(value.x, value.y), std::hypot(value.z, value.w));
  return std::abs(norm - 1.0) <= kQuaternionTolerance;
}

// ENU world -> NED world and FLU body -> FRD body are distinct operations.
inline Vector3 EnuToNed(const Vector3& enu) { return {enu.y, enu.x, -enu.z}; }

inline Vector3 NedToEnu(const Vector3& ned) { return {ned.y, ned.x, -ned.z}; }

inline Vector3 FluToFrd(const Vector3& flu) { return {flu.x, -flu.y, -flu.z}; }

inline Vector3 FrdToFlu(const Vector3& frd) { return {frd.x, -frd.y, -frd.z}; }
}  // namespace raven::contracts
