//===-- llvm/Support/Trace.h - A lightweight process tracer -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TRACE_H
#define LLVM_SUPPORT_TRACE_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Program.h"
#include "llvm/Trace/ScopedObject.h"
#include "llvm/Trace/TraceEvent.h"

#include <chrono>
#include <memory>

namespace llvm {
namespace trace {

class TraceEvent;

/// Specifies how a tracee should continue.  Which values are valid depends
/// on the specific event.  For example, if the child received a signal, we
/// can choose to either deliver the signal to the child, or mask the signal
/// so the child doesn't know it occurred.
enum class RestartAction {
  Default,    // The default restart action is performed, which is dependent
              // on the type of event that occurred.
  MaskEvent,  // The tracer "consumes" the event, and the child is not made
              // aware that it occurred.
  PassToChild // The event is passed onto the child
};

/// Launch a child process and attach to it as the tracer of all of the
/// process's threads at the same time.
///
/// The calling thread is henceforth designated as the "tracer thread".
/// Unless otherwise specified, subsequent calls to trace functions defined
/// in this file must occur on the tracer thread corresponding to a given
/// tracee.
///
/// Returns a native process object representing the tracee that should
/// be used in subsequent calls to the trace apis.
Expected<ScopedProcess>
traceLaunch(StringRef Program, ArrayRef<StringRef> Args,
            Optional<ArrayRef<StringRef>> Env = None,
            ArrayRef<Optional<StringRef>> Redirects = {});

/// Attach to and trace all current and future threads of a running process.
///
/// The calling thread is henceforth designated as the "tracer thread".
/// Unless otherwise specified, subsequent calls to trace functions defined
/// in this file must occur on the tracer thread corresponding to a given
/// tracee.
///
/// Returns a native process object representing the tracee that should
/// be used in subsequent calls to the trace apis.
Expected<ScopedProcess> traceAttach(llvm::sys::procid_t Process);

/// Detach from all threads of a currently traced process, and stop tracing
/// new threads.
///
/// traceDetach can be invoked on an arbitrary thread context and does not
/// need to be invoked from the tracer thread.
///
/// Returns Error::success() if the operation was successful, at which point
/// the process is no longer being traced but continues running normally in
/// the background.  Oterwise, it returns an appropriate error.
Error traceDetach(ScopedProcess Process);

/// Detach from all threads of a currently traced process and stop the process
/// at the same time.
///
/// traceKill can be invoked on an arbitrary thread context and does not need
/// to be invoked from the tracer thread.
///
/// Returns Error::success() if the operation was successful, at which point
/// the process is no longer running.  Oterwise, it returns an appropriate
/// error.
Error traceKill(ScopedProcess Process);

/// Wait indefinitely for a trace event to occur in the specified process.
/// When this function returns, the thread identified by the returned
/// object is stopped and can only be resumed by calling
/// continueFromTraceEvent.
///
/// Returns a TraceEvent object describing the details of the event if the
/// operation was successful, and an error otherwise.
Expected<std::unique_ptr<TraceEvent>> waitForTraceEvent(sys::process_t Process);

/// Wait for the specified duration for a trace event to occur in the
/// specified process.  When this function returns, the thread identified by the
/// returned object is stopped and can only be resumed by calling
/// continueFromTraceEvent.
///
/// If the operation timed out, the function returns llvm::None.  If an event
/// occurred within the specified duration, an appropriate TraceEvent object
/// is returned.  Otherwise, an appropriate error is returned.
Expected<Optional<std::unique_ptr<TraceEvent>>>
waitForTraceEvent(sys::process_t Process, std::chrono::milliseconds Duration);

/// Resume the thread specified by the given TraceEvent object.  The
/// TraceEvent must have been obtained via a call to waitForTraceEvent.
///
/// If the operation was successful, the function returns Error::success() and
/// the thread identified by \p Event resumes running.  Otherwise, an
/// appropriate error is returned.
Error continueFromTraceEvent(std::unique_ptr<TraceEvent> Event,
                             RestartAction Action = RestartAction::Default);
} // namespace sys
} // namespace llvm

#endif
