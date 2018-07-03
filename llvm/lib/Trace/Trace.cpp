//===-- llvm/Support/Trace.cpp - A lightweight process tracer ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Trace/Trace.h"

using namespace llvm;

// Include the truly platform-specific parts.
#if defined(LLVM_ON_UNIX)
#include "Unix/Trace.inc"
#endif
#if defined(_WIN32)
#include "Windows/Trace.inc"
#endif
