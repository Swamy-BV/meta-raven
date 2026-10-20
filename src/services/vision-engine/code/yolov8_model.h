// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <vector>

#include <tensorflow/lite/c/c_api.h>

#include "detection.h"
#include "rgb_input.h"

namespace raven::vision {
struct Letterbox {
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;
};

// EdgeFirst logical YOLOv8: uint8 NHWC input, normalized xywh and sigmoid
// class scores in separate quantized channel-first output tensors.
void ValidateYoloV8(TfLiteInterpreter* interpreter);
Letterbox PrepareYoloV8(TfLiteInterpreter* interpreter, const RgbLayout& image);
std::vector<detection::Box> DecodeYoloV8(const TfLiteInterpreter* interpreter,
                                         const Letterbox& geometry);
}  // namespace raven::vision
