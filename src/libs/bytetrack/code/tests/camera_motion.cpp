// SPDX-License-Identifier: AGPL-3.0-only
#include <BYTETracker.h>

#include <array>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void Require(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

Object Car(const float x) { return {{x, 80, 40, 30}, 2, 0.9F}; }

void CameraPanAndOcclusion() {
  BYTETracker tracker(1.5, 32, 0.5F, 0.7F, 0.8F);
  const auto first = tracker.update({Car(100)}, 1, 1);
  Require(first.size() == 1, "Initial measured track missing");
  const auto identity = first.front().track_id;
  // A pan larger than the box width defeats uncompensated IoU association.
  const std::array<float, 6> pan{1, 0, 80, 0, 1, 0};
  const auto shifted = tracker.update({Car(180)}, 1.1, 3, pan);
  Require(shifted.size() == 1 && shifted.front().track_id == identity,
          "Camera pan broke identity");
  Require(tracker.update({}, 1.2, 3, pan).empty(),
          "Unseen prediction published as measured");
  const auto recovered = tracker.update({Car(340)}, 1.3, 3, pan);
  Require(recovered.size() == 1 && recovered.front().track_id == identity,
          "Measured reacquisition did not recover retained identity");
  const auto& covariance = recovered.front().covariance;
  Require(
      covariance.allFinite() && (covariance.diagonal().array() > 0).all() &&
          (covariance - covariance.transpose()).cwiseAbs().maxCoeff() < 0.001F,
      "Invalid transformed covariance");
  tracker.update({Car(340)}, 3, 3);
  const auto expired = tracker.update({Car(340)}, 3.1, 3);
  Require(expired.size() == 1 && expired.front().track_id != identity,
          "Expired identity resurrected");
  auto invalid = pan;
  invalid[0] = std::numeric_limits<float>::quiet_NaN();
  bool rejected = false;
  try {
    tracker.update({}, 3.2, 3, invalid);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  Require(rejected, "Non-finite camera motion accepted");
}

void OriginalApi() {
  BYTETracker tracker(1.5, 32, 0.5F, 0.7F, 0.8F);
  const auto first = tracker.update({Car(100)}, 1, 1);
  const auto second = tracker.update({Car(101)}, 1.1, 3);
  Require(first.size() == 1 && second.size() == 1 &&
              first.front().track_id == second.front().track_id,
          "Original uncompensated API regressed");
}
}  // namespace

int main() {
  try {
    CameraPanAndOcclusion();
    OriginalApi();
    std::cout
        << "Camera pan, measured recovery, expiry and legacy API passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
