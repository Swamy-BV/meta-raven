// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdint>

#include "rgb_input.h"

// Private parent/worker transport, never a ROS interface. One job is in flight
// at a time: the parent writes before its request, then waits for the reply or
// reaps the failed worker before modifying or releasing the mapping.
class WorkerFrameBuffer final {
 public:
  static constexpr int kInheritedDescriptor = 3;
  WorkerFrameBuffer();
  explicit WorkerFrameBuffer(int inherited);
  ~WorkerFrameBuffer();
  WorkerFrameBuffer(const WorkerFrameBuffer&) = delete;
  WorkerFrameBuffer& operator=(const WorkerFrameBuffer&) = delete;
  WorkerFrameBuffer(WorkerFrameBuffer&&) = delete;
  WorkerFrameBuffer& operator=(WorkerFrameBuffer&&) = delete;

  [[nodiscard]] int ReadDescriptor() const { return reader_; }

  void Write(const RgbLayout& image);
  [[nodiscard]] RgbLayout Read(int width, int height) const;

 private:
  void Release();
  int descriptor_ = -1, reader_ = -1;
  void* mapping_ = nullptr;
  std::uint8_t* writable_ = nullptr;
};
