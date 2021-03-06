// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/vsync_waiter_android.h"

#include <utility>

#include "flutter/common/threads.h"
#include "flutter/fml/platform/android/jni_util.h"
#include "flutter/fml/platform/android/scoped_java_ref.h"
#include "flutter/fml/trace_event.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/logging.h"

namespace shell {

static fml::jni::ScopedJavaGlobalRef<jclass>* g_vsync_waiter_class = nullptr;
static jmethodID g_async_wait_for_vsync_method_ = nullptr;

VsyncWaiterAndroid::VsyncWaiterAndroid() : weak_factory_(this) {}

VsyncWaiterAndroid::~VsyncWaiterAndroid() = default;

void VsyncWaiterAndroid::AsyncWaitForVsync(Callback callback) {
  FTL_DCHECK(!callback_);
  callback_ = std::move(callback);
  ftl::WeakPtr<VsyncWaiterAndroid>* weak =
      new ftl::WeakPtr<VsyncWaiterAndroid>();
  *weak = weak_factory_.GetWeakPtr();

  blink::Threads::Platform()->PostTask([weak] {
    JNIEnv* env = fml::jni::AttachCurrentThread();
    env->CallStaticVoidMethod(g_vsync_waiter_class->obj(),
                              g_async_wait_for_vsync_method_,
                              reinterpret_cast<jlong>(weak));
  });
}

void VsyncWaiterAndroid::OnVsync(int64_t frameTimeNanos,
                                 int64_t frameTargetTimeNanos) {
  Callback callback = std::move(callback_);
  callback_ = Callback();
  blink::Threads::UI()->PostTask([callback, frameTimeNanos, frameTargetTimeNanos] {
    callback(ftl::TimePoint::FromEpochDelta(
                 ftl::TimeDelta::FromNanoseconds(frameTimeNanos)),
             ftl::TimePoint::FromEpochDelta(
                 ftl::TimeDelta::FromNanoseconds(frameTargetTimeNanos)));
  });
}

static void OnNativeVsync(JNIEnv* env,
                          jclass jcaller,
                          jlong frameTimeNanos,
                          jlong frameTargetTimeNanos,
                          jlong cookie) {
  // Note: The tag name must be "VSYNC" (it is special) so that the "Highlight
  // Vsync" checkbox in the timeline can be enabled.
  // See: https://github.com/catapult-project/catapult/blob/2091404475cbba9b786
  // 442979b6ec631305275a6/tracing/tracing/extras/vsync/vsync_auditor.html#L26
  TRACE_EVENT0("flutter", "VSYNC");
  TRACE_EVENT_INSTANT0("flutter", "VSYNC");
  ftl::WeakPtr<VsyncWaiterAndroid>* weak =
      reinterpret_cast<ftl::WeakPtr<VsyncWaiterAndroid>*>(cookie);
  VsyncWaiterAndroid* waiter = weak->get();
  delete weak;
  if (waiter) {
    waiter->OnVsync(static_cast<int64_t>(frameTimeNanos),
                    static_cast<int64_t>(frameTargetTimeNanos));
  }
}

bool VsyncWaiterAndroid::Register(JNIEnv* env) {
  static const JNINativeMethod methods[] = {{
      .name = "nativeOnVsync",
      .signature = "(JJJ)V",
      .fnPtr = reinterpret_cast<void*>(&OnNativeVsync),
  }};

  jclass clazz = env->FindClass("io/flutter/view/VsyncWaiter");

  if (clazz == nullptr) {
    return false;
  }

  g_vsync_waiter_class = new fml::jni::ScopedJavaGlobalRef<jclass>(env, clazz);

  FTL_CHECK(!g_vsync_waiter_class->is_null());

  g_async_wait_for_vsync_method_ = env->GetStaticMethodID(
      g_vsync_waiter_class->obj(), "asyncWaitForVsync", "(J)V");

  FTL_CHECK(g_async_wait_for_vsync_method_ != nullptr);

  return env->RegisterNatives(clazz, methods, arraysize(methods)) == 0;
}

}  // namespace shell
