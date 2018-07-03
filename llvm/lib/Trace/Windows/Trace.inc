//===- llvm/Support/Windows/Trace.inc - Windows Trace Impl ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Windows specific implementation of the Trace API.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic Windows code that
//===          is guaranteed to work on *all* Windows variants.
//===----------------------------------------------------------------------===//

#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/WindowsError.h"
#include "llvm/Trace/TraceEvent.h"

#include "WindowsSupport.h"

using namespace llvm;
using namespace sys;
using namespace trace;

Expected<ScopedProcess>
llvm::trace::traceLaunch(StringRef Program, ArrayRef<StringRef> Args,
                       Optional<ArrayRef<StringRef>> Env,
                       ArrayRef<Optional<StringRef>> Redirects) {
  std::string ErrorMessage;
  bool ExecutionFailed = false;
  llvm::sys::ProcessInfo Info =
      sys::ExecuteNoWait(Program, Args, Env, Redirects, 0U, PLF_Trace,
                         &ErrorMessage, &ExecutionFailed);
  if (ExecutionFailed)
    return llvm::make_error<StringError>(ErrorMessage,
                                         inconvertibleErrorCode());

  return ScopedProcess{ Info.Process };
}

Expected<ScopedProcess> llvm::trace::traceAttach(procid_t Process) {
  return llvm::make_error<StringError>("Not implemented!",
                                       inconvertibleErrorCode());
}

Error llvm::trace::traceDetach(ScopedProcess Process) {
  if (!::DebugActiveProcessStop(::GetProcessId(Process.get())))
    return errorCodeToError(mapWindowsError(::GetLastError()));
  return Error::success();
}

Error llvm::trace::traceKill(ScopedProcess Process) {
  if (!TerminateProcess(Process.get(), 0))
    return errorCodeToError(mapWindowsError(::GetLastError()));
  return Error::success();
}

static std::string getImageFileName(HANDLE FileHandle) {
  DWORD RequiredSizeWithNull = ::GetFinalPathNameByHandleW(FileHandle, nullptr, 0, VOLUME_NAME_DOS | FILE_NAME_OPENED);
  std::vector<wchar_t> Buffer(RequiredSizeWithNull);
  std::string Result;
  DWORD RequiredSizeWithoutNull = ::GetFinalPathNameByHandleW(FileHandle, Buffer.data(), RequiredSizeWithNull, VOLUME_NAME_DOS | FILE_NAME_OPENED);
  if (RequiredSizeWithoutNull != RequiredSizeWithNull - 1)
    return {};
  
  ArrayRef<UTF16> UTF16Array(reinterpret_cast<const UTF16*>(Buffer.data()), RequiredSizeWithoutNull);

  // All strings returned from GetFinalPathNameByHandleW start with \\?\.  Remove that.
  convertUTF16ToUTF8String(UTF16Array.drop_front(4), Result);
  return Result;
}

static Expected<std::unique_ptr<TraceEvent>>
createTraceEvent(process_t Process, const DEBUG_EVENT &DE) {
  switch (DE.dwDebugEventCode) {
  case CREATE_PROCESS_DEBUG_EVENT: {
    const CREATE_PROCESS_DEBUG_INFO & I = DE.u.CreateProcessInfo;
    std::string ImageName = getImageFileName(I.hFile);
    std::unique_ptr<TraceEvent> E(new TraceeConnectedEvent(DE.dwProcessId, DE.dwThreadId, Any{ I },
      ScopedFile{ I.hFile }, std::move(ImageName), uint64_t(I.lpBaseOfImage), uint64_t(I.lpStartAddress), uint64_t(I.lpThreadLocalBase)));
    return std::move(E);
  }
  case EXIT_PROCESS_DEBUG_EVENT: {
    const EXIT_PROCESS_DEBUG_INFO & I = DE.u.ExitProcess;
    auto Result =
      make_unique<TraceeExitedEvent>(DE.dwProcessId, DE.dwThreadId, Any{ I }, I.dwExitCode);

    return std::unique_ptr<TraceEvent>(Result.release());
  }
  case LOAD_DLL_DEBUG_EVENT: {
    const LOAD_DLL_DEBUG_INFO &I = DE.u.LoadDll;
    std::string ImageName = getImageFileName(I.hFile);
    std::unique_ptr<TraceEvent> E(new LoadLibraryEvent(DE.dwProcessId, DE.dwThreadId, Any{ I },
      ScopedFile{ I.hFile }, std::move(ImageName), uint64_t(I.lpBaseOfDll)));
    return std::move(E);
  }
  case UNLOAD_DLL_DEBUG_EVENT: {
    const UNLOAD_DLL_DEBUG_INFO &I = DE.u.UnloadDll;
    auto Result = make_unique<UnloadLibraryEvent>(
        DE.dwProcessId, DE.dwThreadId, Any{I}, uint64_t(I.lpBaseOfDll));
    return std::unique_ptr<TraceEvent>(Result.release());
  }
  case CREATE_THREAD_DEBUG_EVENT: {
    const CREATE_THREAD_DEBUG_INFO &I = DE.u.CreateThread;
    auto Result = make_unique<CreateThreadTraceEvent>(
      DE.dwProcessId, DE.dwThreadId, Any{ I }, 
        uint64_t(I.lpStartAddress), uint64_t(I.lpThreadLocalBase));
    return std::unique_ptr<TraceEvent>(Result.release());
  }
  case EXIT_THREAD_DEBUG_EVENT: {
    const EXIT_THREAD_DEBUG_INFO &I = DE.u.ExitThread;
    auto Result = make_unique<ExitThreadTraceEvent>(
      DE.dwProcessId, DE.dwThreadId, Any{ I }, I.dwExitCode);
    return std::unique_ptr<TraceEvent>(Result.release());
  }
  case EXCEPTION_DEBUG_EVENT: {
    const EXCEPTION_DEBUG_INFO &I = DE.u.Exception;
    if (I.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
      auto Result = make_unique<InstructionBreakpointTraceEvent>(
          DE.dwProcessId, DE.dwThreadId, Any{I}, !!I.dwFirstChance,
          uint64_t(I.ExceptionRecord.ExceptionAddress));
      return std::unique_ptr<TraceEvent>(Result.release());
    }
    break;
  }
  default:
    break;
  }
  return make_unique<TraceEvent>(TraceEventType::Unknown, DE.dwProcessId, DE.dwThreadId, Any{});
}

Expected<std::unique_ptr<TraceEvent>>
llvm::trace::waitForTraceEvent(process_t Process) {
  DEBUG_EVENT DE = {};
  if (!::WaitForDebugEvent(&DE, INFINITE))
    return errorCodeToError(mapWindowsError(::GetLastError()));

  return createTraceEvent(Process, DE);
}

Expected<Optional<std::unique_ptr<TraceEvent>>>
llvm::trace::waitForTraceEvent(process_t Process,
                             std::chrono::milliseconds Duration) {
  DEBUG_EVENT DE = {};
  if (!::WaitForDebugEvent(&DE, Duration.count())) {
    if (::GetLastError() == ERROR_TIMEOUT)
      return llvm::None;

    return errorCodeToError(mapWindowsError(::GetLastError()));
  }

  return createTraceEvent(Process, DE);
}

static DWORD nativeContinueStatus(RestartAction Action) {
  if (Action == RestartAction::PassToChild)
    return DBG_EXCEPTION_NOT_HANDLED;
  return DBG_CONTINUE;
}

Error llvm::trace::continueFromTraceEvent(std::unique_ptr<TraceEvent> Event,
                                        RestartAction Action) {

  DWORD ContinueStatus = nativeContinueStatus(Action);

  BOOL Result = ::ContinueDebugEvent(Event->pid(), Event->tid(), ContinueStatus);
  if (!Result)
    return errorCodeToError(mapWindowsError(::GetLastError()));
  return Error::success();
}
