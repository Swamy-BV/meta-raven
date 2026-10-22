// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <tensorflow/lite/c/common.h>

// One interpreter per isolated worker; the delegate lives longer than it.
class NeutronBackend {
 public:
  NeutronBackend();
  ~NeutronBackend();
  NeutronBackend(const NeutronBackend&) = delete;
  NeutronBackend& operator=(const NeutronBackend&) = delete;
  NeutronBackend(NeutronBackend&&) = delete;
  NeutronBackend& operator=(NeutronBackend&&) = delete;

  [[nodiscard]] TfLiteDelegate* Get() const { return delegate_; }

  [[nodiscard]] int Partitions() const { return delegated_; }

  [[nodiscard]] int CpuNodes() const { return residual_; }

 private:
  void* library_ = nullptr;
  TfLiteDelegate* delegate_ = nullptr;
  void (*destroy_)(TfLiteDelegate*) = nullptr;
  TfLiteStatus (*prepare_)(TfLiteContext*, TfLiteDelegate*) = nullptr;
  int delegated_ = 0, residual_ = 0;
  static NeutronBackend* g_active;
  static TfLiteStatus CheckedPrepare(TfLiteContext*, TfLiteDelegate*);
};
