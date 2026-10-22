// SPDX-License-Identifier: AGPL-3.0-only
#include "core/tracker.h"

#include <BYTETracker.h>
#include <STrack.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "adapters/bytetrack/appearance.h"

namespace raven::tracking {
namespace {
constexpr double kNanosPerSecond = 1000000000.0;
constexpr double kNominalFramesPerSecond = 30.0;
constexpr double kMinDimension = 0.01;
constexpr double kMaxCoordinate = 65536.0;
constexpr std::size_t kMaxClasses = 16;
constexpr std::size_t kMaxTracks = 64;
constexpr std::size_t kMaxDetections = 256;
constexpr std::size_t kMaxClassLength = 128;
constexpr double kMaxLostSeconds = 10.0;
constexpr double kHalf = 0.5;

bool Probability(double value) {
  return std::isfinite(value) && value > 0 && value <= 1;
}

void Validate(const Configuration& value) {
  if (!Probability(value.low_threshold) || !Probability(value.high_threshold) ||
      !Probability(value.new_threshold) ||
      !Probability(value.match_threshold) ||
      value.low_threshold >= value.high_threshold ||
      value.high_threshold > value.new_threshold ||
      !std::isfinite(value.lost_seconds) || value.lost_seconds <= 0 ||
      value.lost_seconds > kMaxLostSeconds || value.max_classes == 0 ||
      value.max_classes > kMaxClasses || value.max_tracks_per_class == 0 ||
      value.max_tracks_per_class > kMaxTracks || value.max_detections == 0 ||
      value.max_detections > kMaxDetections) {
    throw std::invalid_argument("invalid tracking configuration");
  }
}

void ValidateAppearance(const std::vector<float>& descriptor) {
  if (descriptor.empty()) {
    return;
  }
  constexpr std::size_t kDescriptorSize = 512;
  constexpr double kNormTolerance = 0.01;
  double norm = 0;
  for (const auto element : descriptor) {
    if (!std::isfinite(element)) {
      throw std::invalid_argument("non-finite appearance descriptor");
    }
    norm += static_cast<double>(element) * static_cast<double>(element);
  }
  if (descriptor.size() != kDescriptorSize ||
      std::abs(norm - 1.0) > kNormTolerance) {
    throw std::invalid_argument("invalid appearance descriptor size or norm");
  }
}

void Validate(const Observation& value) {
  ValidateAppearance(value.appearance);
  const auto& box = value.box;
  if (value.class_id.empty() || value.class_id.size() > kMaxClassLength ||
      !Probability(value.score) || !std::isfinite(box.center_x) ||
      !std::isfinite(box.center_y) || !std::isfinite(box.width) ||
      !std::isfinite(box.height) || box.width < kMinDimension ||
      box.height < kMinDimension || box.width > kMaxCoordinate ||
      box.height > kMaxCoordinate || box.center_x < 0 || box.center_y < 0 ||
      box.center_x > kMaxCoordinate || box.center_y > kMaxCoordinate) {
    throw std::invalid_argument("invalid detection geometry, class or score");
  }
}

std::string InstanceId() {
  std::random_device random;
  std::ostringstream text;
  text << std::hex << random() << '-' << random();
  return text.str();
}

Object UpstreamObject(const Observation& value, const int label) {
  Object object{};
  object.rect = {
      static_cast<float>(value.box.center_x - value.box.width * kHalf),
      static_cast<float>(value.box.center_y - value.box.height * kHalf),
      static_cast<float>(value.box.width),
      static_cast<float>(value.box.height)};
  object.prob = static_cast<float>(value.score);
  object.label = label;
  return object;
}
}  // namespace

void ValidateObservation(const Observation& observation) {
  Validate(observation);
}

class Tracker::Impl {
 public:
  explicit Impl(Configuration configuration)
      : config_(configuration), instance_(InstanceId()) {
    Validate(config_);
  }

  std::vector<Track> Update(const std::vector<Observation>& observations,
                            std::int64_t stamp_ns);
  void Reset();
  [[nodiscard]] std::size_t RetainedCount() const;
  [[nodiscard]] AppearanceStatistics AppearanceHealth() const;

 private:
  struct ClassState {
    std::unique_ptr<BYTETracker> tracker;
    std::size_t number = 0;
    AppearanceAssociation appearance;
  };

  struct GroupedObservations {
    std::vector<Object> objects;
    std::vector<std::size_t> indices;
  };

  using Groups = std::map<std::string, GroupedObservations>;
  [[nodiscard]] Groups Group(const std::vector<Observation>& observations);
  int LabelCode(const std::string& label);
  [[nodiscard]] std::string GroupKey(const Observation& observation) const;
  void AddClasses(const Groups& groups);
  void Prune();
  [[nodiscard]] Track Output(const STrack& value, const std::string& label,
                             std::size_t number) const;
  AppearanceStatistics retired_;
  Configuration config_;
  const std::string instance_;
  std::uint64_t generation_ = 0;
  std::size_t next_class_ = 0;
  std::optional<std::int64_t> last_stamp_;
  std::map<std::string, ClassState> classes_;
  std::vector<std::string> labels_;
};

Tracker::Impl::Groups Tracker::Impl::Group(
    const std::vector<Observation>& observations) {
  if (observations.size() > config_.max_detections) {
    throw std::invalid_argument("detection count exceeds configured limit");
  }
  Groups groups;
  for (std::size_t index = 0; index < observations.size(); ++index) {
    const auto& observation = observations[index];
    Validate(observation);
    if (observation.score >= config_.low_threshold) {
      const auto group = GroupKey(observation);
      groups[group].objects.push_back(UpstreamObject(
          observation,
          config_.class_agnostic ? LabelCode(observation.class_id) : 0));
      groups[group].indices.push_back(index);
    }
  }
  return groups;
}

std::string Tracker::Impl::GroupKey(const Observation& observation) const {
  if (!config_.class_agnostic) {
    return observation.class_id;
  }
  if (!config_.use_appearance) {
    return "all";
  }
  // A vehicle embedding cannot associate a pedestrian. Merge only subjects of
  // that model; retain ordinary class-aware tracking for all other categories.
  return observation.appearance_subject ? "appearance"
                                        : "label:" + observation.class_id;
}

int Tracker::Impl::LabelCode(const std::string& label) {
  const auto found = std::find(labels_.begin(), labels_.end(), label);
  if (found != labels_.end()) {
    return static_cast<int>(found - labels_.begin());
  }
  if (labels_.size() >= kMaxDetections) {
    throw std::invalid_argument(
        "model class vocabulary exceeds configured bound");
  }
  labels_.push_back(label);
  return static_cast<int>(labels_.size() - 1);
}

void Tracker::Impl::AddClasses(const Groups& groups) {
  auto count = classes_.size();
  for (const auto& [label, objects] : groups) {
    if (classes_.count(label) == 0) {
      ++count;
    }
  }
  if (count > config_.max_classes) {
    throw std::invalid_argument("class count exceeds configured limit");
  }
  for (const auto& [label, objects] : groups) {
    if (classes_.count(label) != 0) {
      continue;
    }
    ClassState state;
    if (next_class_ == std::numeric_limits<std::size_t>::max()) {
      throw std::overflow_error("tracking class counter exhausted");
    }
    state.number = next_class_++;
    state.tracker = std::make_unique<BYTETracker>(
        config_.lost_seconds, static_cast<int>(config_.max_tracks_per_class),
        static_cast<float>(config_.high_threshold),
        static_cast<float>(config_.new_threshold),
        static_cast<float>(config_.match_threshold));
    classes_.emplace(label, std::move(state));
  }
}

Track Tracker::Impl::Output(const STrack& value, const std::string& label,
                            std::size_t number) const {
  if (value.tlwh.size() != 4) {
    throw std::runtime_error("invalid ByteTrack output shape");
  }
  Track track;
  track.observation.box = {static_cast<double>(value.tlwh[0]) +
                               static_cast<double>(value.tlwh[2]) * kHalf,
                           static_cast<double>(value.tlwh[1]) +
                               static_cast<double>(value.tlwh[3]) * kHalf,
                           value.tlwh[2], value.tlwh[3]};
  if (config_.class_agnostic &&
      (value.label < 0 ||
       static_cast<std::size_t>(value.label) >= labels_.size())) {
    throw std::runtime_error("invalid retained ByteTrack category");
  }
  track.observation.class_id =
      config_.class_agnostic ? labels_[value.label] : label;
  track.observation.score =
      config_.class_agnostic ? value.label_score : value.score;
  Validate(track.observation);
  track.id = instance_ + ':' + std::to_string(generation_) + ':' +
             std::to_string(number) + ':' + std::to_string(value.track_id);
  return track;
}

std::vector<Track> Tracker::Impl::Update(
    const std::vector<Observation>& observations, std::int64_t stamp_ns) {
  if (stamp_ns <= 0 || (last_stamp_ && stamp_ns <= *last_stamp_)) {
    throw std::invalid_argument(
        "tracking requires increasing acquisition time");
  }
  if (last_stamp_ &&
      static_cast<double>(stamp_ns - *last_stamp_) / kNanosPerSecond >
          config_.lost_seconds) {
    Reset();
  }
  const auto groups = Group(observations);
  AddClasses(groups);
  const auto step =
      last_stamp_
          ? static_cast<float>(static_cast<double>(stamp_ns - *last_stamp_) /
                               kNanosPerSecond * kNominalFramesPerSecond)
          : 1.0F;
  std::vector<Track> result;
  const GroupedObservations empty;
  for (auto& [label, state] : classes_) {
    const auto found = groups.find(label);
    const auto& group = found == groups.end() ? empty : found->second;
    const auto tracks = state.tracker->update(
        group.objects, static_cast<double>(stamp_ns) / kNanosPerSecond, step,
        config_.use_appearance
            ? state.appearance.Bind(
                  observations, group.indices,
                  static_cast<double>(stamp_ns) / kNanosPerSecond)
            : AssociationHooks{});
    state.appearance.Prune(state.tracker->retained_ids());
    for (const auto& track : tracks) {
      result.push_back(Output(track, label, state.number));
    }
  }
  last_stamp_ = stamp_ns;
  Prune();
  return result;
}

void Tracker::Impl::Prune() {
  for (auto entry = classes_.begin(); entry != classes_.end();) {
    if (entry->second.tracker->size() == 0) {
      retired_.recoveries += entry->second.appearance.Recoveries();
      retired_.rejections += entry->second.appearance.Rejections();
      entry = classes_.erase(entry);
    } else {
      ++entry;
    }
  }
}

void Tracker::Impl::Reset() {
  if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("tracking generation exhausted");
  }
  classes_.clear();
  retired_ = {};
  labels_.clear();
  next_class_ = 0;
  last_stamp_.reset();
  ++generation_;
}

std::size_t Tracker::Impl::RetainedCount() const {
  std::size_t count = 0;
  for (const auto& [label, state] : classes_) {
    count += state.tracker->size();
  }
  return count;
}

AppearanceStatistics Tracker::Impl::AppearanceHealth() const {
  AppearanceStatistics result = retired_;
  for (const auto& [label, state] : classes_) {
    result.recoveries += state.appearance.Recoveries();
    result.rejections += state.appearance.Rejections();
  }
  return result;
}

Tracker::Tracker(Configuration configuration)
    : impl_(std::make_unique<Impl>(configuration)) {}

Tracker::~Tracker() = default;

std::vector<Track> Tracker::Update(const std::vector<Observation>& observations,
                                   std::int64_t stamp_ns) {
  return impl_->Update(observations, stamp_ns);
}

void Tracker::Reset() { impl_->Reset(); }

AppearanceStatistics Tracker::AppearanceHealth() const {
  return impl_->AppearanceHealth();
}

std::size_t Tracker::RetainedCount() const { return impl_->RetainedCount(); }
}  // namespace raven::tracking
