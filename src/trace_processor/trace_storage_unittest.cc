/*
 * Copyright (C) 2017 The Android Open foo Project
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

#include "src/trace_processor/trace_storage.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;

TEST(TraceStorageTest, NoInteractionFirstSched) {
  TraceStorage storage;

  uint32_t cpu = 3;
  uint64_t timestamp = 100;
  uint32_t prev_pid = 2;
  uint32_t prev_state = 32;
  static const char kTestString[] = "test";
  uint32_t next_pid = 4;
  storage.InsertSchedSwitch(cpu, timestamp, prev_pid, prev_state, kTestString,
                            sizeof(kTestString) - 1, next_pid);

  ASSERT_EQ(storage.SlicesForCpu(cpu), nullptr);
}

TEST(TraceStorageTest, InsertSecondSched) {
  TraceStorage storage;

  uint32_t cpu = 3;
  uint64_t timestamp = 100;
  uint32_t pid_1 = 2;
  uint32_t prev_state = 32;
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  uint32_t pid_2 = 4;
  storage.InsertSchedSwitch(cpu, timestamp, pid_1, prev_state, kCommProc1,
                            sizeof(kCommProc1) - 1, pid_2);
  storage.InsertSchedSwitch(cpu, timestamp + 1, pid_2, prev_state, kCommProc2,
                            sizeof(kCommProc2) - 1, pid_1);

  const auto& timestamps = storage.SlicesForCpu(cpu)->start_timestamps();
  ASSERT_EQ(timestamps.size(), 1ul);
  ASSERT_EQ(timestamps[0], timestamp);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
