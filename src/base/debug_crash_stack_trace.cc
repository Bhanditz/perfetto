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

#include <cxxabi.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unwind.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/utils.h"

#if defined(NDEBUG)
#error This translation unit should not be used in release builds
#endif

#if BUILDFLAG(OS_LINUX) || BUILDFLAG(OS_ANDROID)
#if defined(__i386__)
#define SYSCALL_REG(_ctx, _reg) ((_ctx)->uc_mcontext.gregs[(_reg)])
#define SYSCALL_PARM1(_ctx) SYSCALL_REG(_ctx, REG_EBX)
#define SYSCALL_PARM2(_ctx) SYSCALL_REG(_ctx, REG_ECX)
#define SYSCALL_PARM3(_ctx) SYSCALL_REG(_ctx, REG_EDX)
#define SYSCALL_PARM4(_ctx) SYSCALL_REG(_ctx, REG_ESI)
#define SYSCALL_PARM5(_ctx) SYSCALL_REG(_ctx, REG_EDI)
#define SYSCALL_PARM6(_ctx) SYSCALL_REG(_ctx, REG_EBP)
#elif defined(__x86_64__)
#define SYSCALL_REG(_ctx, _reg) ((_ctx)->uc_mcontext.gregs[(_reg)])
#define SYSCALL_PARM1(_ctx) SYSCALL_REG(_ctx, REG_RDI)
#define SYSCALL_PARM2(_ctx) SYSCALL_REG(_ctx, REG_RSI)
#define SYSCALL_PARM3(_ctx) SYSCALL_REG(_ctx, REG_RDX)
#define SYSCALL_PARM4(_ctx) SYSCALL_REG(_ctx, REG_R10)
#define SYSCALL_PARM5(_ctx) SYSCALL_REG(_ctx, REG_R8)
#define SYSCALL_PARM6(_ctx) SYSCALL_REG(_ctx, REG_R9)
#elif defined(__arm__)
#define SYSCALL_REG(_ctx, _reg) ((_ctx)->uc_mcontext.arm_##_reg)
#define SYSCALL_PARM1(_ctx) SYSCALL_REG(_ctx, r0)
#define SYSCALL_PARM2(_ctx) SYSCALL_REG(_ctx, r1)
#define SYSCALL_PARM3(_ctx) SYSCALL_REG(_ctx, r2)
#define SYSCALL_PARM4(_ctx) SYSCALL_REG(_ctx, r3)
#define SYSCALL_PARM5(_ctx) SYSCALL_REG(_ctx, r4)
#define SYSCALL_PARM6(_ctx) SYSCALL_REG(_ctx, r5)
#elif defined(__aarch64__)
#define SYSCALL_REG(_ctx, _reg) ((_ctx)->uc_mcontext.regs[_reg])
#define SYSCALL_PARM1(_ctx) SYSCALL_REG(_ctx, 0)
#define SYSCALL_PARM2(_ctx) SYSCALL_REG(_ctx, 1)
#define SYSCALL_PARM3(_ctx) SYSCALL_REG(_ctx, 2)
#define SYSCALL_PARM4(_ctx) SYSCALL_REG(_ctx, 3)
#define SYSCALL_PARM5(_ctx) SYSCALL_REG(_ctx, 4)
#define SYSCALL_PARM6(_ctx) SYSCALL_REG(_ctx, 5)
#endif
#endif  // BUILDFLAG(OS_LINUX) || BUILDFLAG(OS_ANDROID)

namespace {

constexpr size_t kDemangledNameLen = 4096;

bool g_sighandler_registered = false;
char* g_demangled_name = nullptr;

struct SigHandler {
  int sig_num;
  struct sigaction old_handler;
};

SigHandler g_signals[] = {{SIGSEGV, {}}, {SIGILL, {}}, {SIGTRAP, {}},
                          {SIGABRT, {}}, {SIGBUS, {}}, {SIGFPE, {}},
                          {SIGSYS, {}}};

template <typename T>
void Print(const T& str) {
  write(STDERR_FILENO, str, sizeof(str) - 1);
}

template <typename T>
void PrintDec(T n, T base = 10) {
  char buf[128 + 2] = {};
  char* c = nullptr;
  for (c = &buf[sizeof(buf) - 2]; n && c >= buf; c--, n /= base) {
    char digit = static_cast<char>(n % base) & 0x0F;
    *c = (digit < 10) ? '0' + digit : 'A' + digit - 10;
  }
  c++;
  write(STDERR_FILENO, c, strlen(c));
}

template <typename T>
void PrintHex(T n) {
  PrintDec(n, static_cast<T>(16));
}

struct StackCrawlState {
  StackCrawlState(uintptr_t* frames_arg, size_t max_depth_arg)
      : frames(frames_arg),
        frame_count(0),
        max_depth(max_depth_arg),
        skip_count(1) {}

  uintptr_t* frames;
  size_t frame_count;
  size_t max_depth;
  size_t skip_count;
};

_Unwind_Reason_Code TraceStackFrame(_Unwind_Context* context, void* arg) {
  StackCrawlState* state = static_cast<StackCrawlState*>(arg);
  uintptr_t ip = _Unwind_GetIP(context);

  if (ip != 0 && state->skip_count) {
    state->skip_count--;
    return _URC_NO_REASON;
  }

  state->frames[state->frame_count++] = ip;
  if (state->frame_count >= state->max_depth)
    return _URC_END_OF_STACK;
  return _URC_NO_REASON;
}

// Note: use only async-safe functions inside this.
void SignalHandler(int sig_num, siginfo_t* info, void* ucontext) {
  // Restore the old handlers.
  for (size_t i = 0; i < sizeof(g_signals) / sizeof(g_signals[0]); i++)
    sigaction(g_signals[i].sig_num, &g_signals[i].old_handler, nullptr);

  Print("\n------------------ BEGINNING OF CRASH ------------------\n");
  Print("Signal: ");
  const char* sig_name = strsignal(sig_num);
  write(STDERR_FILENO, sig_name, strlen(sig_name));
#if BUILDFLAG(OS_LINUX) || BUILDFLAG(OS_ANDROID)
  if (sig_num == SIGSYS) {
    ucontext_t* ctx = static_cast<ucontext_t*>(ucontext);
    Print("\nSyscall: ");
    PrintDec(info->si_syscall);
    Print("  arch: ");
    PrintHex(info->si_arch);
    Print("\nArgs: (");
    PrintHex(SYSCALL_PARM1(ctx));
    Print(", ");
    PrintHex(SYSCALL_PARM2(ctx));
    Print(", ");
    PrintHex(SYSCALL_PARM3(ctx));
    Print(", ");
    PrintHex(SYSCALL_PARM4(ctx));
    Print(", ");
    PrintHex(SYSCALL_PARM5(ctx));
    Print(", ");
    PrintHex(SYSCALL_PARM6(ctx));
    Print(")");
  }
#endif  // BUILDFLAG(OS_LINUX) || BUILDFLAG(OS_ANDROID)
  Print("\n");

  Print("Fault addr: ");
  PrintHex(reinterpret_cast<uintptr_t>(info->si_addr));
  Print("\n\nBacktrace:\n");

  const size_t kMaxFrames = 64;
  uintptr_t frames[kMaxFrames];
  StackCrawlState unwind_state(frames, kMaxFrames);
  _Unwind_Backtrace(&TraceStackFrame, &unwind_state);

  for (uint8_t i = 0; i < unwind_state.frame_count; i++) {
    Dl_info sym_info = {};
    int res = dladdr(reinterpret_cast<void*>(frames[i]), &sym_info);
    Print("\n#");
    PrintDec(i);
    Print("  ");
    const char* sym_name = res ? sym_info.dli_sname : nullptr;
    if (sym_name) {
      int ignored;
      size_t len = kDemangledNameLen;
      char* demangled = abi::__cxa_demangle(sym_info.dli_sname,
                                            g_demangled_name, &len, &ignored);
      if (demangled) {
        sym_name = demangled;
        // In the exceptional case of demangling something > kDemangledNameLen,
        // __cxa_demangle will realloc(). In that case the malloc()-ed pointer
        // might be moved.
        g_demangled_name = demangled;
      }
      write(STDERR_FILENO, sym_name, strlen(sym_name));
    } else {
      if (res && sym_info.dli_fname) {
        write(STDERR_FILENO, sym_info.dli_fname, strlen(sym_info.dli_fname));
        Print(" ");
      }
      PrintHex(frames[i] - reinterpret_cast<uintptr_t>(sym_info.dli_fbase));
    }
    Print("\n");
  }

  Print("------------------ END OF CRASH ------------------\n");
}

// __attribute__((constructor)) causes a static initializer that automagically
// early runs this function before the main().
void __attribute__((constructor)) EnableStacktraceOnCrashForDebug();

void EnableStacktraceOnCrashForDebug() {
  if (g_sighandler_registered)
    return;
  g_sighandler_registered = true;

  // Pre-allocate the string for __cxa_demangle() to reduce the risk of that
  // invoking realloc() within the signal handler.
  g_demangled_name = reinterpret_cast<char*>(malloc(kDemangledNameLen));
  struct sigaction sigact;
  sigact.sa_sigaction = &SignalHandler;
  sigact.sa_flags = static_cast<decltype(sigact.sa_flags)>(
      SA_RESTART | SA_SIGINFO | SA_RESETHAND);
  for (size_t i = 0; i < sizeof(g_signals) / sizeof(g_signals[0]); i++)
    sigaction(g_signals[i].sig_num, &sigact, &g_signals[i].old_handler);
}

}  // namespace
