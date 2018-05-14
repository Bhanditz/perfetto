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

#include <emscripten/emscripten.h>
#include <map>
#include <string>

#include "perfetto/trace_processor/blob_reader.h"
#include "perfetto/trace_processor/query.pb.h"
#include "perfetto/trace_processor/sched.h"
#include "perfetto/trace_processor/sched.pb.h"
#include "src/trace_processor/emscripten_task_runner.h"

namespace perfetto {
namespace trace_processor {

using RequestID = uint32_t;

// +---------------------------------------------------------------------------+
// | Imported functions implemented by JS and injected via Initialize().       |
// +---------------------------------------------------------------------------+

// ReadTrace(): reads a portion of the trace file. Ca
// Invoked by the C++ code in the trace processor to ask the embedder (e.g. the
// JS code for the case of the UI) to get read a chunk of the trace file.
// Args:
//   offset: the start offset (in bytes) in the trace file to read.
//   len: maximum size of the buffered returned.
// Returns:
//   The embedder is supposed to asynchronously call ReadComplete(), passing
//   back the offset, together with the actual buffer.
using ReadTraceFunction = void (*)(uint32_t /*offset*/, uint32_t /*len*/);

// Reply(): replies to a RPC method invocation (e.g., sched_getSchedEvents()).
// Called asynchronously (i.e. in a separate task) by the C++ code inside the
// trace processor to return data for a RPC method call.
// The function is generic and thankfully we need just one for all methods
// because the output is always a protobuf buffer.
// Args:
//  RequestID: the ID passed by the embedder when invoking the RPC method (e.g.,
//             the first argument passed to sched_getSchedEvents()).
using ReplyFunction = void (*)(RequestID,
                               bool success,
                               const char* /*proto_reply_data*/,
                               uint32_t /*len*/);

// +---------------------------------------------------------------------------+
// | Boring boilerplate                                                        |
// +---------------------------------------------------------------------------+
namespace {

// TODO(primiano): create a class to handle the module state, instad of relying
// on globals and get rid of this hack.
#pragma GCC diagnostic ignored "-Wglobal-constructors"
#pragma GCC diagnostic ignored "-Wexit-time-destructors"

EmscriptenTaskRunner* g_task_runner;
ReadTraceFunction g_read_trace;
ReplyFunction g_reply;
BlobReader::ReadCallback g_read_callback;

// Implements the BlobReader interface passed to the trace processor C++
// classes. It simply routes the requests to the embedder (e.g. JS/TS).
class BlobReaderImpl : public BlobReader {
 public:
  explicit BlobReaderImpl(base::TaskRunner* task_runner)
      : task_runner_(task_runner) {}
  ~BlobReaderImpl() override = default;

  void Read(uint32_t offset, size_t max_size, ReadCallback callback) override {
    g_read_trace(offset, max_size);
    g_read_callback = callback;
    (void)task_runner_;
  }

 private:
  base::TaskRunner* const task_runner_;
};

BlobReaderImpl* blob_reader() {
  static BlobReaderImpl* instance = new BlobReaderImpl(g_task_runner);
  return instance;
}

Sched* sched() {
  static Sched* instance = new Sched(g_task_runner, blob_reader());
  return instance;
}

}  // namespace

// +---------------------------------------------------------------------------+
// | Exported functions called by the JS/TS running in the worker.             |
// +---------------------------------------------------------------------------+
extern "C" {
void EMSCRIPTEN_KEEPALIVE Initialize(ReadTraceFunction, ReplyFunction);
void Initialize(ReadTraceFunction read_trace_function,
                ReplyFunction reply_function) {
  printf("Initializing WASM bridge\n");
  g_task_runner = new EmscriptenTaskRunner();
  g_read_trace = read_trace_function;
  g_reply = reply_function;
}

void EMSCRIPTEN_KEEPALIVE ReadComplete(uint32_t, const uint8_t*, uint32_t);
void ReadComplete(uint32_t offset, const uint8_t* data, uint32_t size) {
  g_read_callback(offset, data, size);
}

// +---------------------------------------------------------------------------+
// Here we should have one function for each method of each RPC service defined
// in trace_processor/*.proto.
// TODO(primiano): autogenerate these, their implementation is fully mechanical.
// +---------------------------------------------------------------------------+

void EMSCRIPTEN_KEEPALIVE sched_getSchedEvents(RequestID, const uint8_t*, int);
void sched_getSchedEvents(RequestID req_id,
                          const uint8_t* query_data,
                          int len) {
  protos::Query query;
  bool parsed = query.ParseFromArray(query_data, len);
  if (!parsed) {
    std::string err = "Failed to parse input request";
    g_reply(req_id, false, err.data(), err.size());
  }

  // When the C++ class implementing the service replies, serialize the protobuf
  // result and post it back to the worker script (|g_reply|).
  auto callback = [req_id](const protos::SchedEvents& events) {
    std::string encoded;
    events.SerializeToString(&encoded);
    g_reply(req_id, true, encoded.data(),
            static_cast<uint32_t>(encoded.size()));
  };

  sched()->GetSchedEvents(query, callback);
}

}  // extern "C"

}  // namespace trace_processor
}  // namespace perfetto
