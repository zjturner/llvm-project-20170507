
add_llvm_library(LLVMTrace
  Trace.cpp

  ADDITIONAL_HEADER_DIRS
  Unix
  Windows
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Trace
  )
