//===- ScopedHandle.h - Scoped Kernel Object --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRACE_SCOPED_OBJECT_H
#define LLVM_TRACE_SCOPED_OBJECT_H

#include "llvm/Support/FileSystem.h"

namespace llvm {
namespace trace {
template <typename Traits> class ScopedObject {
  typedef typename Traits::object_type object_type;
  object_type Object;

  ScopedObject(const ScopedObject &other) = delete;
  void operator=(const ScopedObject &other) = delete;

public:
  ScopedObject() : Object(Traits::getInvalid()) {}

  explicit ScopedObject(object_type Object) : Object(Object) {}

  ScopedObject(ScopedObject &&Other) {
    Object = Other.Object;
    Other.Object = Traits::getInvalid();
  }

  ~ScopedObject() {
    if (Traits::isValid(Object))
      Traits::close(Object);
  }

  object_type take() {
    object_type O = Object;
    Object = Traits::getInvalid();
    return O;
  }

  object_type get() const { return Object; }

  ScopedObject &operator=(object_type O) {
    if (Traits::isValid(O))
      Traits::close(O);
    Object = O;
    return *this;
  }

  bool valid() const { return Traits::isValid(Object); }

  operator object_type() const { return Object; }
};

#if _WIN32
struct FileObjectTraits {
  using object_type = void *;

  static object_type getInvalid() { return object_type(-1); }
  static bool isValid(object_type F) {
    return F != nullptr && F != getInvalid();
  }
  static void close(object_type F) { llvm::sys::fs::closeFile(F); }
};
using ProcessObjectTraits = FileObjectTraits;
using ThreadObjectTraits = FileObjectTraits;
#else
struct FileObjectTraits {
  using object_type = int;

  static object_type getInvalid() { return -1; }
  static bool isValid(object_type F) { return F != -1; }
  static void close(object_type F) { return llvm::sys::closeFile(F); }
};

struct ProcessObjectTraits {
  using object_type = int;

  static object_type getInvalid() { return -1; }
  static bool isValid(object_type F) { return F != -1; }
  static void close(object_type) {}
};

using ThreadObjectTraits = ProcessObjectTraits;
#endif

using ScopedFile = ScopedObject<FileObjectTraits>;
using ScopedProcess = ScopedObject<ProcessObjectTraits>;
using ScopedThread = ScopedObject<ThreadObjectTraits>;
}
}
#endif
