// SPDX-License-Identifier: AGPL-3.0-only
#include "neutron_backend.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

NeutronBackend* NeutronBackend::g_active = nullptr;

NeutronBackend::NeutronBackend() {
  if (g_active ||
      access("/dev/neutron0", static_cast<int>(static_cast<unsigned>(R_OK) |
                                               static_cast<unsigned>(W_OK)))) {
    throw std::runtime_error("Neutron device unavailable or access denied");
  }
  library_ = dlopen("libneutron_delegate.so",
                    static_cast<int>(static_cast<unsigned>(RTLD_NOW) |
                                     static_cast<unsigned>(RTLD_LOCAL)));
  if (!library_) {
    throw std::runtime_error("Neutron delegate library unavailable");
  }
  using Create =
      TfLiteDelegate* (*)(char**, char**, size_t, void (*)(const char*));
  auto create = reinterpret_cast<Create>(
      dlsym(library_, "tflite_plugin_create_delegate"));
  destroy_ = reinterpret_cast<void (*)(TfLiteDelegate*)>(
      dlsym(library_, "tflite_plugin_destroy_delegate"));
  if (create && destroy_) {
    delegate_ = create(nullptr, nullptr, 0, nullptr);
  }
  if (!delegate_ || !delegate_->Prepare) {
    if (delegate_ && destroy_) {
      destroy_(delegate_);
    }
    dlclose(library_);
    throw std::runtime_error("cannot create Neutron delegate");
  }
  prepare_ = delegate_->Prepare;
  g_active = this;
  delegate_->Prepare = CheckedPrepare;
}

NeutronBackend::~NeutronBackend() {
  if (delegate_) {
    delegate_->Prepare = prepare_;
    destroy_(delegate_);
  }
  g_active = nullptr;
  if (library_) {
    dlclose(library_);
  }
}

static int CountCompiled(TfLiteContext* context, const TfLiteIntArray* plan) {
  int compiled = 0;
  for (int i = 0; i < plan->size; ++i) {
    TfLiteNode* node = nullptr;
    TfLiteRegistration* registration = nullptr;
    if (context->GetNodeAndRegistration(context, plan->data[i], &node,
                                        &registration) != kTfLiteOk) {
      return -1;
    }
    const char* name = registration->custom_name;
    if (name && (!std::strcmp(name, "NeutronOp") ||
                 !std::strcmp(name, "NeutronGraph"))) {
      ++compiled;
    }
  }
  return compiled;
}

TfLiteStatus NeutronBackend::CheckedPrepare(TfLiteContext* context,
                                            TfLiteDelegate* delegate) {
  auto* self = g_active;
  if (!self || self->delegate_ != delegate) {
    return kTfLiteError;
  }
  TfLiteIntArray* plan = nullptr;
  if (context->GetExecutionPlan(context, &plan) != kTfLiteOk) {
    return kTfLiteError;
  }
  const int compiled = CountCompiled(context, plan);
  // Original CPU models must never be advertised as NPU workloads.
  if (compiled <= 0 || self->prepare_(context, delegate) != kTfLiteOk ||
      context->GetExecutionPlan(context, &plan) != kTfLiteOk) {
    return kTfLiteError;
  }
  self->delegated_ = self->residual_ = 0;
  for (int i = 0; i < plan->size; ++i) {
    TfLiteNode* node = nullptr;
    TfLiteRegistration* registration = nullptr;
    if (context->GetNodeAndRegistration(context, plan->data[i], &node,
                                        &registration) != kTfLiteOk) {
      return kTfLiteError;
    }
    if (node->delegate == delegate) {
      ++self->delegated_;
    } else {
      ++self->residual_;
    }
  }
  return self->delegated_ > 0 ? kTfLiteOk : kTfLiteError;
}
