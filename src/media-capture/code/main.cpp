// SPDX-License-Identifier: AGPL-3.0-only
#include <sys/stat.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <rclcpp/init_options.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/gstreamer/capture.h"
#include "adapters/ros/node.h"
#include "media_config.h"

int main(int argc, char** argv) {
  constexpr auto kPrivateFileUmask = 0077;
  try {
    if (rclcpp::remove_ros_arguments(argc, argv).size() != 1) {
      throw std::invalid_argument("use ROS parameters and remapping arguments");
    }
    umask(kPrivateFileUmask);
    rclcpp::init(argc, argv, rclcpp::InitOptions{},
                 rclcpp::SignalHandlerOptions::None);
    int result = 0;
    {
      raven::media::MediaCaptureNode output;
      result = RunCapture(output.Settings(), output);
    }
    rclcpp::shutdown();
    return result;
  } catch (const std::exception& error) {
    std::cerr << "media-capture: " << error.what() << '\n';
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
}
