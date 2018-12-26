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

#include "src/traced/probes/logcat/logcat_data_source.h"

#include "perfetto/base/logging.h"
#include "perfetto/base/optional.h"
#include "perfetto/base/scoped_file.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/base/unix_socket.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/trace_writer.h"

#include "perfetto/config/android/android_logcat_config.pb.h"
#include "perfetto/trace/android/android_logcat.pbzero.h"
#include "perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {
constexpr uint32_t kMinPollRateMs = 100;
constexpr uint32_t kDefaultPollRateMs = 1000;
constexpr size_t kBufSize = base::kPageSize;

// From Android's liblog/include/log/log_read.h
struct logger_entry_v4 {
  uint16_t len;      /* length of the payload */
  uint16_t hdr_size; /* sizeof(struct logger_entry_v4) */
  int32_t pid;       /* generating process's pid */
  uint32_t tid;      /* generating process's tid */
  uint32_t sec;      /* seconds since Epoch */
  uint32_t nsec;     /* nanoseconds */
  uint32_t lid;      /* log id of the payload, bottom 4 bits currently */
  uint32_t uid;      /* generating process's uid */
};

}  // namespace

LogcatDataSource::LogcatDataSource(DataSourceConfig ds_config,
                                   base::TaskRunner* task_runner,
                                   TracingSessionID session_id,
                                   std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, kTypeId),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      logcat_sock_(base::UnixSocketRaw::CreateInvalid()),
      weak_factory_(this) {
  const auto& cfg = ds_config.android_logcat_config();
  poll_rate_ms_ = cfg.poll_ms() ? cfg.poll_ms() : kDefaultPollRateMs;
  poll_rate_ms_ = std::max(kMinPollRateMs, poll_rate_ms_);

  std::vector<uint32_t> log_ids;
  if (cfg.log_ids_size() == 0) {
    // If no log id is specified add them all.
    for (uint32_t id = 0; id <= protos::AndroidLogcatLogId_MAX; id++)
      log_ids.push_back(id);
  } else {
    for (uint32_t id : cfg.log_ids())
      log_ids.push_back(id);
  }

  // Build a linear vector out of the tag filters and keep track of the string
  // boundaries. Once done, derive StringView(s) out of the vector. This is
  // to create a set<StringView> which is backed by contiguous chars, to improve
  // cache hotness while doing per-entry string matches.
  std::vector<std::pair<size_t, size_t>> tag_boundaries;
  for (const std::string& tag : cfg.filter_tags()) {
    const size_t begin = filter_tags_strbuf_.size();
    filter_tags_strbuf_.insert(filter_tags_strbuf_.end(), tag.begin(),
                               tag.end());
    const size_t end = filter_tags_strbuf_.size();
    tag_boundaries.emplace_back(begin, end - begin);
  }

  filter_tags_strbuf_.shrink_to_fit();
  // At this point pointers to |filter_tags_strbuf_| are stable.

  for (const auto& it : tag_boundaries)
    filter_tags_.emplace(&filter_tags_strbuf_[it.first], it.second);

  mode_ = "stream lids";
  for (auto it = log_ids.begin(); it != log_ids.end(); it++) {
    mode_ += it == log_ids.begin() ? "=" : ",";
    mode_ += std::to_string(*it);
  }

  min_prio_ = cfg.min_prio();
  buf_ = base::PagedMemory::Allocate(kBufSize);
}

LogcatDataSource::~LogcatDataSource() = default;

void LogcatDataSource::Start() {
  static const char kLogcatSocket[] = "/dev/socket/logdr";
  logcat_sock_ = base::UnixSocketRaw::CreateMayFail(base::SockType::kSeqPacket);
  if (!logcat_sock_ || !logcat_sock_.Connect(kLogcatSocket)) {
    PERFETTO_PLOG("Could not connecto to %s", kLogcatSocket);
    return;
  }
  PERFETTO_DLOG("Starting logcat stream: %s", mode_.c_str());
  if (logcat_sock_.Send(mode_.data(), mode_.size()) <= 0) {
    PERFETTO_PLOG("send() failed on logcat socket %s", kLogcatSocket);
    return;
  }
  logcat_sock_.SetBlocking(false);
  PERFETTO_LOG("perfetto_logcat_clock_sync CLOCK_BOOTTIME=%lld",
               base::GetWallTimeNs().count());
  Tick(/*post_next_task=*/true);
}

void LogcatDataSource::Tick(bool post_next_task) {
  if (post_next_task) {
    auto now_ms = base::GetWallTimeMs().count();
    auto weak_this = weak_factory_.GetWeakPtr();
    task_runner_->PostDelayedTask(
        [weak_this] {
          if (weak_this)
            weak_this->Tick(/*post_next_task=*/true);
        },
        poll_rate_ms_ - (now_ms % poll_rate_ms_));
  }

  ssize_t rsize;
  TraceWriter::TracePacketHandle packet;
  protos::pbzero::AndroidLogcatPacket* logcat_packet = nullptr;
  size_t events_read = 0;
  while ((rsize = logcat_sock_.Receive(buf_.Get(), kBufSize)) > 0) {
    // TODO max messages per packet. DNS
    events_read++;
    stats_.num_total++;
    char* buf = reinterpret_cast<char*>(buf_.Get());
    PERFETTO_DCHECK(
        reinterpret_cast<uintptr_t>(buf) % alignof(logger_entry_v4) == 0);
    size_t hdr_size = reinterpret_cast<logger_entry_v4*>(buf)->hdr_size;
    if (hdr_size >= static_cast<size_t>(rsize)) {
      PERFETTO_DLOG("Invalid hdr_size in logcat message (%zu <= %zd)", hdr_size,
                    rsize);
      stats_.num_parse_failures++;
      continue;
    }

    // The ABI of logger_entry can change in Android desserts. Copy that in a
    // temporary struct, so that unset fields stay zero-initialized.
    logger_entry_v4 entry{};
    memcpy(&entry, buf, hdr_size);
    buf += hdr_size;
    if (static_cast<ssize_t>(entry.len) > rsize) {
      PERFETTO_DLOG("Invalid len in logcat message (%hu <= %zd)", entry.len,
                    rsize);
      stats_.num_parse_failures++;
      continue;
    }
    char* end = buf + entry.len;

    if (!packet) {
      // Lazily add the packet on the first message received.
      packet = writer_->NewTracePacket();
      packet->set_timestamp(
          static_cast<uint64_t>(base::GetBootTimeNs().count()));
      logcat_packet = packet->set_logcat();
    }

    int prio = 0;
    base::StringView tag;
    base::StringView msg;
    if (entry.lid != *buf > 0 && *buf < 16) {  // TODO check log id instead? DNS
      // Format:
      // [Priority 1 byte] [ tag ] [ NUL ] [ message ]
      prio = *(buf++);
      if (prio < min_prio_) {
        stats_.num_skipped++;
        continue;
      }
      char* str_end = buf;
      for (; str_end < end && *str_end; str_end++) {
      }
      if (str_end >= end - 2) {
        // Looks like there is a tag-less message.
        msg = base::StringView(buf, static_cast<size_t>(str_end - buf));
      } else {
        tag = base::StringView(buf, static_cast<size_t>(str_end - buf));
        buf = str_end + 1;  // Move |buf| to the start of the message.
        size_t msg_len = static_cast<size_t>(end - buf);

        // Protobuf strings don't need the nul terminator. If the string is
        // null terminator, omit the terminator from the length.
        if (msg_len > 0 && *(end - 1) == '\0')
          msg_len--;
        msg = base::StringView(buf, msg_len);
      }
      buf = str_end;

      if (!filter_tags_.empty() && filter_tags_.count(tag) == 0) {
        stats_.num_skipped++;
        continue;
      }
    } else {
      // TODO implement event log.
    }

    auto* evt = logcat_packet->add_messages();
    uint64_t ts = entry.sec * 1000000000ULL + entry.nsec;
    evt->set_timestamp(ts);
    evt->set_log_id(static_cast<protos::pbzero::AndroidLogcatLogId>(entry.lid));
    evt->set_pid(entry.pid);
    evt->set_tid(entry.tid);
    if (prio)
      evt->set_prio(static_cast<protos::pbzero::AndroidLogcatPriority>(prio));
    if (!tag.empty())
      evt->set_tag(tag.data(), tag.size());
    if (!msg.empty())
      evt->set_message(msg.data(), msg.size());
  }                                        // while(logcat_sock_.Receive())
  PERFETTO_ILOG("Seen %zu", events_read);  // DNS
}

void LogcatDataSource::Flush(FlushRequestID, std::function<void()> callback) {
  // Grab most recent entries.
  Tick(/*post_next_task=*/false);

  // Emit stats.
  {
    auto packet = writer_->NewTracePacket();
    packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
    auto* stats = packet->set_logcat()->set_stats();
    stats->set_num_total(stats_.num_total);
    stats->set_num_skipped(stats_.num_skipped);
    stats->set_num_parse_failures(stats_.num_parse_failures);
  }

  writer_->Flush(callback);
}

}  // namespace perfetto
