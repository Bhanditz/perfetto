/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_PERFETTO_CMD_PERFETTO_CMD_H_
#define SRC_PERFETTO_CMD_PERFETTO_CMD_H_

#include <memory>
#include <string>
#include <vector>

#include <time.h>

#include "perfetto/base/scoped_file.h"
#include "perfetto/base/unix_task_runner.h"
#include "perfetto/tracing/core/consumer.h"
#include "perfetto/tracing/ipc/consumer_ipc_client.h"
#include "rate_limiter.h"

#include "src/perfetto_cmd/perfetto_cmd_state.pb.h"

#if defined(PERFETTO_OS_ANDROID)
#include "perfetto/base/android_task_runner.h"
#endif  // defined(PERFETTO_OS_ANDROID)

#if defined(PERFETTO_BUILD_WITH_ANDROID)
#include <android/os/DropBoxManager.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>
#endif  // defined(PERFETTO_BUILD_WITH_ANDROID)

namespace perfetto {

#if defined(PERFETTO_OS_ANDROID)
using PlatformTaskRunner = base::AndroidTaskRunner;
#else
using PlatformTaskRunner = base::UnixTaskRunner;
#endif

class PerfettoCmd : public Consumer, RateLimiter::Delegate {
 public:
  // De-serialize state from fd.
  static bool ReadState(int in_fd, PerfettoCmdState* state);
  // Serialize state to fd.
  static bool WriteState(int out_fd, const PerfettoCmdState& state);

  int Main(int argc, char** argv);
  int PrintUsage(const char* argv0);
  void OnStopTraceTimer();
  void OnTimeout();

  // perfetto::Consumer implementation.
  void OnConnect() override;
  void OnDisconnect() override;
  void OnTraceData(std::vector<TracePacket>, bool has_more) override;

  // RateLimiter::Delegate implementation.
  virtual bool LoadState(PerfettoCmdState* state) override;
  virtual bool SaveState(const PerfettoCmdState& state) override;
  virtual bool DoTrace(uint64_t* uploaded_bytes) override;

 private:
  bool OpenOutputFile();
  uint64_t GetTimestamp();
  std::string GetStatePath();

  PlatformTaskRunner task_runner_;
  std::unique_ptr<perfetto::Service::ConsumerEndpoint> consumer_endpoint_;
  std::unique_ptr<TraceConfig> trace_config_;
  base::ScopedFstream trace_out_stream_;
  std::string trace_out_path_;

  // Only used if linkat(AT_FDCWD) isn't available.
  std::string tmp_trace_out_path_;

  std::string dropbox_tag_;
  bool did_process_full_trace_ = false;
};

}  // perfetto

#endif  // SRC_PERFETTO_CMD_PERFETTO_CMD_H_
