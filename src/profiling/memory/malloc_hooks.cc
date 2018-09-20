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

#include <inttypes.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <private/bionic_malloc_dispatch.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/export.h"
#include "src/profiling/memory/client.h"
#include "src/profiling/memory/sampler.h"

static const MallocDispatch* g_dispatch;
static perfetto::Client* g_client;
constexpr const char* kHeapprofdSock = "/dev/socket/heapprofd";
constexpr size_t kNumConnections = 2;

// This is so we can make an so that we can swap out with the existing
// libc_malloc_hooks.so
#ifndef HEAPPROFD_PREFIX
#define HEAPPROFD_PREFIX heapprofd
#endif

#define HEAPPROFD_ADD_PREFIX(name) \
  PERFETTO_BUILDFLAG_CAT(HEAPPROFD_PREFIX, name)

__BEGIN_DECLS

PERFETTO_EXPORT bool HEAPPROFD_ADD_PREFIX(_initialize)(
    const MallocDispatch* malloc_dispatch,
    int* malloc_zygote_child,
    const char* options);
PERFETTO_EXPORT void HEAPPROFD_ADD_PREFIX(_finalize)();
PERFETTO_EXPORT void HEAPPROFD_ADD_PREFIX(_dump_heap)(const char* file_name);
PERFETTO_EXPORT void HEAPPROFD_ADD_PREFIX(_get_malloc_leak_info)(
    uint8_t** info,
    size_t* overall_size,
    size_t* info_size,
    size_t* total_memory,
    size_t* backtrace_size);
PERFETTO_EXPORT bool HEAPPROFD_ADD_PREFIX(_write_malloc_leak_info)(FILE* fp);
PERFETTO_EXPORT ssize_t HEAPPROFD_ADD_PREFIX(
    _malloc_backtrace)(void* pointer, uintptr_t* frames, size_t frame_count);
PERFETTO_EXPORT void HEAPPROFD_ADD_PREFIX(_free_malloc_leak_info)(
    uint8_t* info);
PERFETTO_EXPORT size_t HEAPPROFD_ADD_PREFIX(_malloc_usable_size)(void* pointer);
PERFETTO_EXPORT void* HEAPPROFD_ADD_PREFIX(_malloc)(size_t size);
PERFETTO_EXPORT void HEAPPROFD_ADD_PREFIX(_free)(void* pointer);
PERFETTO_EXPORT void* HEAPPROFD_ADD_PREFIX(_aligned_alloc)(size_t alignment,
                                                           size_t size);
PERFETTO_EXPORT void* HEAPPROFD_ADD_PREFIX(_memalign)(size_t alignment,
                                                      size_t bytes);
PERFETTO_EXPORT void* HEAPPROFD_ADD_PREFIX(_realloc)(void* pointer,
                                                     size_t bytes);
PERFETTO_EXPORT void* HEAPPROFD_ADD_PREFIX(_calloc)(size_t nmemb, size_t bytes);
struct mallinfo HEAPPROFD_ADD_PREFIX(_mallinfo)();
PERFETTO_EXPORT int HEAPPROFD_ADD_PREFIX(_mallopt)(int param, int value);
PERFETTO_EXPORT int HEAPPROFD_ADD_PREFIX(_posix_memalign)(void** memptr,
                                                          size_t alignment,
                                                          size_t size);
PERFETTO_EXPORT int HEAPPROFD_ADD_PREFIX(_iterate)(
    uintptr_t base,
    size_t size,
    void (*callback)(uintptr_t base, size_t size, void* arg),
    void* arg);
PERFETTO_EXPORT void HEAPPROFD_ADD_PREFIX(_malloc_disable)();
PERFETTO_EXPORT void HEAPPROFD_ADD_PREFIX(_malloc_enable)();

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
PERFETTO_EXPORT void* HEAPPROFD_ADD_PREFIX(_pvalloc)(size_t bytes);
PERFETTO_EXPORT void* HEAPPROFD_ADD_PREFIX(_valloc)(size_t size);
#endif

__END_DECLS

bool HEAPPROFD_ADD_PREFIX(_initialize)(const MallocDispatch* malloc_dispatch,
                                       int*,
                                       const char*) {
  g_dispatch = malloc_dispatch;
  g_client = new perfetto::Client(kHeapprofdSock, kNumConnections);
  return true;
}

void HEAPPROFD_ADD_PREFIX(_finalize)() {}

void HEAPPROFD_ADD_PREFIX(_dump_heap)(const char*) {}

void HEAPPROFD_ADD_PREFIX(
    _get_malloc_leak_info)(uint8_t**, size_t*, size_t*, size_t*, size_t*) {}

bool HEAPPROFD_ADD_PREFIX(_write_malloc_leak_info)(FILE*) {
  return false;
}

ssize_t HEAPPROFD_ADD_PREFIX(_malloc_backtrace)(void*, uintptr_t*, size_t) {
  return -1;
}

void HEAPPROFD_ADD_PREFIX(_free_malloc_leak_info)(uint8_t*) {}

size_t HEAPPROFD_ADD_PREFIX(_malloc_usable_size)(void* pointer) {
  return g_dispatch->malloc_usable_size(pointer);
}

void* HEAPPROFD_ADD_PREFIX(_malloc)(size_t size) {
  void* addr = g_dispatch->malloc(size);
  if (g_client->ShouldSampleAlloc(size, g_dispatch->malloc))
    g_client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void HEAPPROFD_ADD_PREFIX(_free)(void* pointer) {
  g_client->RecordFree(reinterpret_cast<uint64_t>(pointer));
  return g_dispatch->free(pointer);
}

void* HEAPPROFD_ADD_PREFIX(_aligned_alloc)(size_t alignment, size_t size) {
  void* addr = g_dispatch->aligned_alloc(alignment, size);
  if (g_client->ShouldSampleAlloc(size, g_dispatch->malloc))
    g_client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void* HEAPPROFD_ADD_PREFIX(_memalign)(size_t alignment, size_t size) {
  void* addr = g_dispatch->memalign(alignment, size);
  if (g_client->ShouldSampleAlloc(size, g_dispatch->malloc))
    g_client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void* HEAPPROFD_ADD_PREFIX(_realloc)(void* pointer, size_t size) {
  g_client->RecordFree(reinterpret_cast<uint64_t>(pointer));
  void* addr = g_dispatch->realloc(pointer, size);
  if (g_client->ShouldSampleAlloc(size, g_dispatch->malloc))
    g_client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

void* HEAPPROFD_ADD_PREFIX(_calloc)(size_t nmemb, size_t size) {
  void* addr = g_dispatch->calloc(nmemb, size);
  if (g_client->ShouldSampleAlloc(size, g_dispatch->malloc))
    g_client->RecordMalloc(size, reinterpret_cast<uint64_t>(addr));
  return addr;
}

struct mallinfo HEAPPROFD_ADD_PREFIX(_mallinfo)() {
  return g_dispatch->mallinfo();
}

int HEAPPROFD_ADD_PREFIX(_mallopt)(int param, int value) {
  return g_dispatch->mallopt(param, value);
}

int HEAPPROFD_ADD_PREFIX(_posix_memalign)(void** memptr,
                                          size_t alignment,
                                          size_t size) {
  return g_dispatch->posix_memalign(memptr, alignment, size);
}

int HEAPPROFD_ADD_PREFIX(_iterate)(uintptr_t,
                                   size_t,
                                   void (*)(uintptr_t base,
                                            size_t size,
                                            void* arg),
                                   void*) {
  return 0;
}

void HEAPPROFD_ADD_PREFIX(_malloc_disable)() {
  return g_dispatch->malloc_disable();
}

void HEAPPROFD_ADD_PREFIX(_malloc_enable)() {
  return g_dispatch->malloc_enable();
}

#if defined(HAVE_DEPRECATED_MALLOC_FUNCS)
void* HEAPPROFD_ADD_PREFIX(_pvalloc)(size_t bytes) {
  return g_dispatch->palloc(size);
}

void* HEAPPROFD_ADD_PREFIX(_valloc)(size_t size) {
  return g_dispatch->valloc(size);
}

#endif
