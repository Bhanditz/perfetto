/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_STATS_H_
#define SRC_TRACE_PROCESSOR_STATS_H_

namespace perfetto {
namespace trace_processor {
namespace stats {

// Compile time list of parsing and processing stats.
// clang-format off
#define PERFETTO_TP_STATS(F)                                                  \
  F(android_log_num_failed,                     kSingle,  kError, kTrace),    \
  F(android_log_num_skipped,                    kSingle,  kError, kTrace),    \
  F(android_log_num_total,                      kSingle,  kInfo,  kTrace),    \
  F(counter_events_out_of_order,                kSingle,  kError, kAnalysis), \
  F(invalid_clock_snapshots,                    kSingle,  kError, kAnalysis), \
  F(invalid_cpu_times,                          kSingle,  kError, kAnalysis), \
  F(meminfo_unknown_keys,                       kSingle,  kError, kAnalysis), \
  F(mismatched_sched_switch_tids,               kSingle,  kError, kAnalysis), \
  F(proc_stat_unknown_counters,                 kSingle,  kError, kAnalysis), \
  F(rss_stat_unknown_keys,                      kSingle,  kError, kAnalysis), \
  F(sched_switch_out_of_order,                  kSingle,  kError, kAnalysis), \
  F(ftrace_bundle_tokenizer_errors,             kSingle,  kError, kAnalysis), \
  F(ftrace_begin_cpu_entries,                   kIndexed, kInfo,  kTrace),    \
  F(ftrace_begin_cpu_overrun,                   kIndexed, kError, kTrace),    \
  F(ftrace_begin_cpu_commit_overrun,            kIndexed, kError, kTrace),    \
  F(ftrace_begin_cpu_bytes_read,                kIndexed, kInfo,  kTrace),    \
  F(ftrace_begin_cpu_oldest_event_ts,           kIndexed, kInfo,  kTrace),    \
  F(ftrace_begin_cpu_now_ts,                    kIndexed, kInfo,  kTrace),    \
  F(ftrace_begin_cpu_dropped_events,            kIndexed, kError, kTrace),    \
  F(ftrace_begin_cpu_read_events,               kIndexed, kInfo,  kTrace),    \
  F(ftrace_end_cpu_entries,                     kIndexed, kInfo,  kTrace),    \
  F(ftrace_end_cpu_overrun,                     kIndexed, kError, kTrace),    \
  F(ftrace_end_cpu_commit_overrun,              kIndexed, kError, kTrace),    \
  F(ftrace_end_cpu_bytes_read,                  kIndexed, kInfo,  kTrace),    \
  F(ftrace_end_cpu_oldest_event_ts,             kIndexed, kInfo,  kTrace),    \
  F(ftrace_end_cpu_now_ts,                      kIndexed, kInfo,  kTrace),    \
  F(ftrace_end_cpu_dropped_events,              kIndexed, kError, kTrace),    \
  F(ftrace_end_cpu_read_events,                 kIndexed, kInfo,  kTrace),    \
  F(traced_producers_connected,                 kSingle,  kInfo,  kTrace),    \
  F(traced_producers_seen,                      kSingle,  kInfo,  kTrace),    \
  F(traced_data_sources_registered,             kSingle,  kInfo,  kTrace),    \
  F(traced_data_sources_seen,                   kSingle,  kInfo,  kTrace),    \
  F(traced_tracing_sessions,                    kSingle,  kInfo,  kTrace),    \
  F(traced_total_buffers,                       kSingle,  kInfo,  kTrace),    \
  F(traced_buf_bytes_written,                   kIndexed, kInfo,  kTrace),    \
  F(traced_buf_chunks_written,                  kIndexed, kInfo,  kTrace),    \
  F(traced_buf_chunks_overwritten,              kIndexed, kInfo,  kTrace),    \
  F(traced_buf_write_wrap_count,                kIndexed, kInfo,  kTrace),    \
  F(traced_buf_patches_succeeded,               kIndexed, kInfo,  kTrace),    \
  F(traced_buf_patches_failed,                  kIndexed, kInfo,  kTrace),    \
  F(traced_buf_readaheads_succeeded,            kIndexed, kInfo,  kTrace),    \
  F(traced_buf_readaheads_failed,               kIndexed, kInfo,  kTrace),    \
  F(vmstat_unknown_keys,                        kSingle,  kError, kAnalysis)
// clang-format on

enum Type {
  kSingle,  // Single-value property, one value per key.
  kIndexed  // Indexed property, multiple value per key (e.g. cpu_stats[1]).
};

enum Severity {
  kInfo,  // Diagnostic counters
  kError  // If any kError counter is > 0 the UI will raise an error.
};

enum Source {
  // The counter is collected when recording the trace on-device and is just
  // being reflected in the stats table.
  kTrace,

  // The counter is genrated when importing / processing the trace in the trace
  // processor.
  kAnalysis
};

// Declares an enum of literals (one for each stat). The enum values of each
// literal corresponds to the string index in the arrays below.
#define PERFETTO_TP_STATS_ENUM(name, ...) name
enum KeyIDs : size_t { PERFETTO_TP_STATS(PERFETTO_TP_STATS_ENUM), kNumKeys };

// The code below declares an array for each property (name, type, ...).

#define PERFETTO_TP_STATS_NAME(name, ...) #name
constexpr char const* kNames[] = {PERFETTO_TP_STATS(PERFETTO_TP_STATS_NAME)};

#define PERFETTO_TP_STATS_TYPE(_, type, ...) type
constexpr Type kTypes[] = {PERFETTO_TP_STATS(PERFETTO_TP_STATS_TYPE)};

#define PERFETTO_TP_STATS_SEVERITY(_, __, severity, ...) severity
constexpr Severity kSeverities[] = {
    PERFETTO_TP_STATS(PERFETTO_TP_STATS_SEVERITY)};

#define PERFETTO_TP_STATS_SOURCE(_, __, ___, source, ...) source
constexpr Source kSources[] = {PERFETTO_TP_STATS(PERFETTO_TP_STATS_SOURCE)};

}  // namespace stats
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_STATS_H_
