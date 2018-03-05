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

#ifndef SRC_PERFETTO_CMD_RATE_LIMITER_H_
#define SRC_PERFETTO_CMD_RATE_LIMITER_H_

#include "src/perfetto_cmd/perfetto_cmd_state.pb.h"

namespace perfetto {

class RateLimiter {
 public:
  struct Args {
    bool is_dropbox = false;
    bool ignore_guardrails = false;
    uint64_t current_timestamp = 0;
  };

  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual bool LoadState(PerfettoCmdState* state) = 0;
    virtual bool SaveState(const PerfettoCmdState& state) = 0;
    virtual bool DoTrace(uint64_t* bytes = nullptr) = 0;
  };

  RateLimiter(Delegate* delegate);
  virtual ~RateLimiter();

  int Run(const Args& args);

 private:
  Delegate* delegate_;
};

}  // namespace perfetto

#endif  // SRC_PERFETTO_CMD_RATE_LIMITER_H_
