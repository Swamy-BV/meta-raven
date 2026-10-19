// SPDX-License-Identifier: AGPL-3.0-only
#include <sys/stat.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <rclcpp/executors.hpp>
#include <rclcpp/utilities.hpp>

#include "adapters/ros/node.h"
#include "inference_worker.h"

namespace {
constexpr auto kWorkerArgumentCount = 7;
constexpr auto kBackendArgument = 5;
constexpr auto kThreadsArgument = 6;
constexpr auto kPrivateFileUmask = 0077;

void ConfigureProcess() {
  std::signal(SIGPIPE, SIG_IGN);
  // Set native library limits before ROS or inference starts additional
  // threads. NOLINTBEGIN(concurrency-mt-unsafe)
  if (setenv("OPENBLAS_NUM_THREADS", "1", 1) != 0 ||
      setenv("OMP_NUM_THREADS", "2", 1) != 0) {
    throw std::runtime_error("cannot set worker thread limits");
  }
  // NOLINTEND(concurrency-mt-unsafe)
  umask(kPrivateFileUmask);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == kWorkerArgumentCount &&
        std::string(argv[1]) == "--inference-worker") {
      return InferenceWorker(argv[2], argv[3], argv[4], argv[kBackendArgument],
                             std::stoi(argv[kThreadsArgument]));
    }
    ConfigureProcess();
    rclcpp::init(argc, argv);
    // Destroy the node and join its job thread before shutting down ROS.
    {
      const auto node = std::make_shared<raven::vision::VisionNode>();
      rclcpp::spin(node);
    }
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "vision-engine: " << error.what() << '\n';
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
}
