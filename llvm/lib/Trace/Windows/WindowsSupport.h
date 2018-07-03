//===- WindowsSupport.h - Common Windows Include File -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Always include WindowsSupport.h instead of including <windows.h> directly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRACE_WINDOWSSUPPORT_H
#define LLVM_TRACE_WINDOWSSUPPORT_H

// mingw-w64 tends to define it as 0x0502 in its headers.
#undef _WIN32_WINNT
#undef _WIN32_IE

// Require at least Windows 7 API.
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0800 // MinGW at it again. FIXME: verify if still needed.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#endif
