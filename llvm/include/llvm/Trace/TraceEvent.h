//===-- llvm/Support/TraceEvent.h - Trace Event Definitions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TRACEEVENT_H
#define LLVM_SUPPORT_TRACEEVENT_H

#include "llvm/ADT/Any.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"

#include <string>

namespace llvm {
namespace trace {
enum class TraceEventType {
  Unknown,
  TraceeConnected,
  TraceeExited,
  LoadLibrary,
  UnloadLibrary,
  CreateThread,
  ExitThread,
  InstructionBreakpoint
};

/// The base class for all trace events, whether native (e.g. maps directly to
/// an operating-system trace event) or synthetic (e.g. not all platforms
/// have native trace events when a new thread is created.  On such platforms,
/// the library can detect this internally and synthesize such an event).
class TraceEvent {
public:
  TraceEvent(TraceEventType Type, sys::procid_t Pid, sys::threadid_t Tid,
             Any PlatformPiece)
      : Type(Type), Pid(Pid), Tid(Tid),
        PlatformPiece(std::move(PlatformPiece)) {}

  virtual ~TraceEvent() {}

  sys::procid_t pid() const { return Pid; }
  sys::threadid_t tid() const { return Tid; }
  TraceEventType type() const { return Type; }

  const Any &platformPiece() const { return PlatformPiece; }

private:
  TraceEventType Type;
  sys::procid_t Pid;
  sys::threadid_t Tid;
  Any PlatformPiece;
};

class TraceeConnectedEvent : public TraceEvent {
public:
  TraceeConnectedEvent(sys::procid_t Pid, sys::threadid_t Tid,
                       Any PlatformPiece, ScopedFile ImageFile,
                       std::string ImageFileName, uint64_t ImageBase,
                       uint64_t EntryPoint, uint64_t ThreadLocalArea)
      : TraceEvent(TraceEventType::TraceeConnected, Pid, Tid,
                   std::move(PlatformPiece)),
        ImageFile(std::move(ImageFile)),
        ImageFileName(std::move(ImageFileName)), EntryPoint(EntryPoint),
        ImageBase(ImageBase), ThreadLocalArea(ThreadLocalArea) {}

  static bool classof(const TraceEvent *E) {
    return E->type() == TraceEventType::TraceeConnected;
  }

  sys::fs::file_t imageFile() const { return ImageFile.get(); }
  StringRef imageFileName() const { return ImageFileName; }
  uint64_t entryPoint() const { return EntryPoint; }
  uint64_t imageBase() const { return ImageBase; }
  uint64_t threadLocalArea() const { return ThreadLocalArea; }

private:
  ScopedFile ImageFile;
  std::string ImageFileName;
  uint64_t EntryPoint;
  uint64_t ImageBase;
  uint64_t ThreadLocalArea;
};

class TraceeExitedEvent : public TraceEvent {
public:
  TraceeExitedEvent(sys::procid_t Pid, sys::threadid_t Tid, Any PlatformPiece,
                    int ExitCode)
      : TraceEvent(TraceEventType::TraceeExited, Pid, Tid,
                   std::move(PlatformPiece)),
        ExitCode(ExitCode) {}

  static bool classof(const TraceEvent *E) {
    return E->type() == TraceEventType::TraceeExited;
  }

  int exitCode() const { return ExitCode; }

private:
  int ExitCode;
};

class LoadLibraryEvent : public TraceEvent {
public:
  LoadLibraryEvent(sys::procid_t Pid, sys::threadid_t Tid, Any PlatformPiece,
                   ScopedFile ImageFile, std::string ImageFileName,
                   uint64_t LoadAddress)
      : TraceEvent(TraceEventType::LoadLibrary, Pid, Tid,
                   std::move(PlatformPiece)),
        ImageFile(std::move(ImageFile)),
        ImageFileName(std::move(ImageFileName)), LoadAddress(LoadAddress) {}

  static bool classof(const TraceEvent *E) {
    return E->type() == TraceEventType::LoadLibrary;
  }

  sys::fs::file_t imageFile() const { return ImageFile.get(); }
  StringRef imageFileName() const { return ImageFileName; }
  uint64_t loadAddress() const { return LoadAddress; }

private:
  ScopedFile ImageFile;
  std::string ImageFileName;
  uint64_t LoadAddress;
};

class UnloadLibraryEvent : public TraceEvent {
public:
  UnloadLibraryEvent(sys::procid_t Pid, sys::threadid_t Tid, Any PlatformPiece,
                     uint64_t LoadAddress)
      : TraceEvent(TraceEventType::UnloadLibrary, Pid, Tid,
                   std::move(PlatformPiece)),
        LoadAddress(LoadAddress) {}

  static bool classof(const TraceEvent *E) {
    return E->type() == TraceEventType::UnloadLibrary;
  }

  uint64_t loadAddress() const { return LoadAddress; }

private:
  uint64_t LoadAddress;
};

class CreateThreadTraceEvent : public TraceEvent {
public:
  CreateThreadTraceEvent(sys::procid_t Pid, sys::threadid_t Tid,
                         Any PlatformPiece, uint64_t ThreadStartAddress,
                         uint64_t ThreadLocalArea)
      : TraceEvent(TraceEventType::CreateThread, Pid, Tid,
                   std::move(PlatformPiece)),
        ThreadStartAddress(ThreadStartAddress),
        ThreadLocalArea(ThreadLocalArea) {}

  static bool classof(const TraceEvent *E) {
    return E->type() == TraceEventType::CreateThread;
  }

  uint64_t threadStartAddress() const { return ThreadStartAddress; }
  uint64_t threadLocalArea() const { return ThreadLocalArea; }

private:
  uint64_t ThreadStartAddress;
  uint64_t ThreadLocalArea;
};

class ExitThreadTraceEvent : public TraceEvent {
public:
  ExitThreadTraceEvent(sys::procid_t Pid, sys::threadid_t Tid,
                       Any PlatformPiece, int ExitCode)
      : TraceEvent(TraceEventType::ExitThread, Pid, Tid,
                   std::move(PlatformPiece)),
        ExitCode(ExitCode) {}

  static bool classof(const TraceEvent *E) {
    return E->type() == TraceEventType::ExitThread;
  }

  int exitCode() const { return ExitCode; }

private:
  int ExitCode;
};

class InstructionBreakpointTraceEvent : public TraceEvent {
public:
  InstructionBreakpointTraceEvent(sys::procid_t Pid, sys::threadid_t Tid,
                                  Any PlatformPiece, bool FirstChance,
                                  uint64_t Address)
      : TraceEvent(TraceEventType::InstructionBreakpoint, Pid, Tid,
                   std::move(PlatformPiece)),
        FirstChance(FirstChance), Address(Address) {}

  static bool classof(const TraceEvent *E) {
    return E->type() == TraceEventType::InstructionBreakpoint;
  }

  bool firstChance() const { return FirstChance; }
  uint64_t address() const { return Address; }

private:
  bool FirstChance;
  uint64_t Address;
};
}
}

#endif
