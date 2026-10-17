// SPDX-License-Identifier: AGPL-3.0-only

#include <sys/stat.h>

#include <exception>
#include <iostream>
#include <memory>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/gstreamer/backend.h"
#include "adapters/ros/node.h"
#include "adapters/storage/photo_store.h"
#include "core/types.h"

int main(int argc, char** argv) {
  constexpr auto kPrivateFileUmask = 0077;
  try {
    umask(kPrivateFileUmask);
    rclcpp::init(argc, argv);
    auto node = std::make_shared<raven::media::MediaCaptureNode>(
        std::make_unique<raven::media::GstreamerBackend>(),
        [](const raven::media::Settings& settings) {
          return std::make_unique<raven::media::PhotoStore>(
              settings.output_dir, settings.max_photos);
        });
    rclcpp::spin(node);
    node->Stop();
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "media-capture: " << error.what() << '\n';
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
}
