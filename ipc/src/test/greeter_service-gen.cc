/*
 * Copyright (C) 2017 The Android Open Source Project
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

// TODO(primiano): this file should be autogenerated by our the protobuf plugin.

#include "greeter_service.pb.h"

#include "ipc/basic_types.h"
#include "ipc/service_descriptor.h"
#include "ipc/src/test/greeter_service-gen.h"

#include <memory>

using ::perfetto::ipc::ServiceDescriptor;

namespace ipc_test {

namespace {

template <typename T>
using _AsyncResult = ::perfetto::ipc::AsyncResult<T>;

template <typename T>
using _Deferred = ::perfetto::ipc::Deferred<T>;

using _ProtoMessage = ::perfetto::ipc::ProtoMessage;

// A templated protobuf message decoder. Returns nullptr in case of failure.
template <typename T>
std::unique_ptr<_ProtoMessage> Decoder(const std::string& proto_data) {
  std::unique_ptr<_ProtoMessage> msg(new T());
  if (msg->ParseFromString(proto_data))
    return msg;
  return nullptr;
}

template <typename T>
std::unique_ptr<_ProtoMessage> Factory() {
  return std::unique_ptr<_ProtoMessage>(new T());
}

ServiceDescriptor* CreateDescriptor() {
  ServiceDescriptor* desc = new ServiceDescriptor();
  desc->service_name = "Greeter";

  // rpc SayHello(GreeterRequestMsg) returns (GreeterReplyMsg) {}
  desc->methods.emplace_back(ServiceDescriptor::Method{
      "SayHello", &Decoder<::ipc_test::GreeterRequestMsg>,
      &Decoder<::ipc_test::GreeterReplyMsg>,
      &Factory<::ipc_test::GreeterReplyMsg>});

  // rpc WaveGoodbye(GreeterRequestMsg) returns (GreeterReplyMsg) {}
  desc->methods.emplace_back(ServiceDescriptor::Method{
      "WaveGoodbye", &Decoder<::ipc_test::GreeterRequestMsg>,
      &Decoder<::ipc_test::GreeterReplyMsg>,
      &Factory<::ipc_test::GreeterReplyMsg>});

  desc->methods.shrink_to_fit();
  return desc;
}

const ServiceDescriptor& GetDescriptorLazy() {
  static ServiceDescriptor* lazily_initialized_descriptor = CreateDescriptor();
  return *lazily_initialized_descriptor;
}

}  // namespace

GreeterProxy::GreeterProxy(
    ::perfetto::ipc::ServiceProxy::EventListener* event_listener)
    : ::perfetto::ipc::ServiceProxy(event_listener) {}
GreeterProxy::~GreeterProxy() = default;

const ServiceDescriptor& GreeterProxy::GetDescriptor() {
  return GetDescriptorLazy();
}

void GreeterProxy::SayHello(const GreeterRequestMsg& request,
                            DeferredGreeterReply reply) {
  BeginInvoke("SayHello", request, reply.MoveAsBase());
}

void GreeterProxy::WaveGoodbye(const GreeterRequestMsg& request,
                               DeferredGreeterReply reply) {
  BeginInvoke("WaveGoodbye", request, reply.MoveAsBase());
}

}  // namespace ipc_test
