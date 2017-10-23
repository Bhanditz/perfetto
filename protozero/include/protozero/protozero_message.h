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

#ifndef PROTOZERO_INCLUDE_PROTOZERO_PROTOZERO_MESSAGE_H_
#define PROTOZERO_INCLUDE_PROTOZERO_PROTOZERO_MESSAGE_H_

#include <type_traits>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "protozero/contiguous_memory_range.h"
#include "protozero/proto_utils.h"
#include "protozero/protozero_message_handle.h"
#include "protozero/scattered_stream_writer.h"

namespace protozero {

// Base class extended by the proto C++ stubs generated by the ProtoZero
// compiler. This class provides the minimal runtime required to support
// append-only operations and is desiged for performance. None of the methods
// require any dynamic memory allocation.
class ProtoZeroMessage {
 public:
  static constexpr uint32_t kMaxNestingDepth = 8;

  // Ctor and Dtor of ProtoZeroMessage are never called, with the exeception
  // of root (non-nested) messages. Nested messages are allocated via placement
  // new in the |nested_messages_arena_| and implictly destroyed when the arena
  // of the root message goes away. This is fine as long as all the fields are
  // PODs, which is checked by the static_assert in the ctor (see the Reset()
  // method in the .cc file).
  ProtoZeroMessage() = default;

  // Clears up the state, allowing the message to be reused as a fresh one.
  void Reset(ScatteredStreamWriter*);

  // Commits all the changes to the buffer (backfills the size field of this and
  // all nested messages) and seals the message. Returns the size of the message
  // (and all nested sub-messages), without taking into account any chunking.
  // Finalize is idempotent and can be called several times w/o side effects.
  size_t Finalize();

  // Optional. If is_valid() == true, the corresponding memory region (its
  // length == proto_utils::kMessageLengthFieldSize) is backfilled with the size
  // of this message (minus |size_already_written| below) when the message is
  // finalized. This is the mechanism used by messages to backfill their
  // corresponding size field in the parent message.
  ContiguousMemoryRange size_field() const { return size_field_; }
  void set_size_field(const ContiguousMemoryRange& reserved_range) {
    size_field_ = reserved_range;
  }

  // This is to deal with case of backfilling the size of a root (non-nested)
  // message which is split into multiple chunks. Upon finalization only the
  // partial size that lies in the last chunk has to be backfilled.
  void inc_size_already_written(size_t size) { size_already_written_ += size; }

#if PROTOZERO_ENABLE_HANDLE_DEBUGGING()
  void set_handle(ProtoZeroMessageHandleBase* handle) { handle_ = handle; }
#endif

  // Proto types: uint64, uint32, int64, int32, bool, enum.
  template <typename T>
  void AppendVarInt(uint32_t field_id, T value) {
    if (nested_message_)
      EndNestedMessage();

    uint8_t buffer[proto_utils::kMaxSimpleFieldEncodedSize];
    uint8_t* pos = buffer;

    pos = proto_utils::WriteVarInt(proto_utils::MakeTagVarInt(field_id), pos);
    // WriteVarInt encodes signed values in two's complement form.
    pos = proto_utils::WriteVarInt(value, pos);
    WriteToStream(buffer, pos);
  }

  // Proto types: sint64, sint32.
  template <typename T>
  void AppendSignedVarInt(uint32_t field_id, T value) {
    AppendVarInt(field_id, proto_utils::ZigZagEncode(value));
  }

  // Proto types: bool, enum (small).
  // Faster version of AppendVarInt for tiny numbers.
  void AppendTinyVarInt(uint32_t field_id, int32_t value) {
    assert(0 <= value && value < 0x80);
    if (nested_message_)
      EndNestedMessage();

    uint8_t buffer[proto_utils::kMaxSimpleFieldEncodedSize];
    uint8_t* pos = buffer;
    // MakeTagVarInt gets super optimized here for constexpr.
    pos = proto_utils::WriteVarInt(proto_utils::MakeTagVarInt(field_id), pos);
    *pos++ = static_cast<uint8_t>(value);
    WriteToStream(buffer, pos);
  }

  // Proto types: fixed64, sfixed64, fixed32, sfixed32, double, float.
  template <typename T>
  void AppendFixed(uint32_t field_id, T value) {
    if (nested_message_)
      EndNestedMessage();

    uint8_t buffer[proto_utils::kMaxSimpleFieldEncodedSize];
    uint8_t* pos = buffer;

    pos = proto_utils::WriteVarInt(proto_utils::MakeTagFixed<T>(field_id), pos);
    memcpy(pos, &value, sizeof(T));
    pos += sizeof(T);
    // TODO: Optimize memcpy performance, see http://crbug.com/624311 .
    WriteToStream(buffer, pos);
  }

  void AppendString(uint32_t field_id, const char* str);
  void AppendBytes(uint32_t field_id, const void* value, size_t size);

  // Begins a nested message, using the static storage provided by the parent
  // class (see comment in |nested_messages_arena_|). The nested message ends
  // either when Finalize() is called or when any other Append* method is called
  // in the parent class.
  // The template argument T is supposed to be a stub class auto generated from
  // a .proto, hence a subclass of ProtoZeroMessage.
  template <class T>
  T* BeginNestedMessage(uint32_t field_id) {
    // This is to prevent subclasses (which should be autogenerated, though), to
    // introduce extra state fields (which wouldn't be initialized by Reset()).
    static_assert(std::is_base_of<ProtoZeroMessage, T>::value,
                  "T must be a subclass of ProtoZeroMessage");
    static_assert(sizeof(T) == sizeof(ProtoZeroMessage),
                  "ProtoZeroMessage subclasses cannot introduce extra state.");
    T* message = reinterpret_cast<T*>(nested_messages_arena_);
    BeginNestedMessageInternal(field_id, message);
    return message;
  }

 private:
  ProtoZeroMessage(const ProtoZeroMessage&) = delete;
  ProtoZeroMessage& operator=(const ProtoZeroMessage&) = delete;

  void BeginNestedMessageInternal(uint32_t field_id, ProtoZeroMessage*);

  // Called by Finalize and Append* methods.
  void EndNestedMessage();

  void WriteToStream(const uint8_t* src_begin, const uint8_t* src_end) {
#if PROTOZERO_ENABLE_HANDLE_DEBUGGING()
    assert(!sealed_);
#endif
    assert(src_begin < src_end);
    const size_t size = static_cast<size_t>(src_end - src_begin);
    stream_writer_->WriteBytes(src_begin, size);
    size_ += size;
  }

  // Only POD fields are allowed. This class's dtor is never called.
  // See the comment on the static_assert in the the corresponding .cc file.

  // The stream writer interface used for the serialization.
  ScatteredStreamWriter* stream_writer_;

  // Keeps track of the size of the current message.
  size_t size_;

  ContiguousMemoryRange size_field_;
  size_t size_already_written_;

  // Used to detect attemps to create messages with a nesting level >
  // kMaxNestingDepth. |nesting_depth_| == 0 for root (non-nested) messages.
  uint8_t nesting_depth_;

#if PROTOZERO_ENABLE_HANDLE_DEBUGGING()
  // When true, no more changes to the message are allowed. This is to DCHECK
  // attempts of writing to a message which has been Finalize()-d.
  bool sealed_;

  ProtoZeroMessageHandleBase* handle_;
#endif

  // Pointer to the last child message created through BeginNestedMessage(), if
  // any, nullptr otherwise. There is no need to keep track of more than one
  // message per nesting level as the proto-zero API contract mandates that
  // nested fields can be filled only in a stacked fashion. In other words,
  // nested messages are finalized and sealed when any other field is set in the
  // parent message (or the parent message itself is finalized) and cannot be
  // accessed anymore afterwards.
  ProtoZeroMessage* nested_message_;

  // The root message owns the storage for all its nested messages, up to a max
  // of kMaxNestingDepth levels (see the .cc file). Note that the boundaries of
  // the arena are meaningful only for the root message.
  // Unfortunately we cannot put the sizeof() math here because we cannot sizeof
  // the current class in a header. However the .cc file has a static_assert
  // that guarantees that (see the Reset() method in the .cc file).
  alignas(sizeof(void*)) uint8_t nested_messages_arena_[512];

  // DO NOT add any fields below |nested_messages_arena_|. The memory layout of
  // nested messages would overflow the storage allocated by the root message.
};

}  // namespace protozero

#endif  // PROTOZERO_INCLUDE_PROTOZERO_PROTOZERO_MESSAGE_H_
