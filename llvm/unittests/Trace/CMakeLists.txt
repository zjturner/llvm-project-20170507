set(LLVM_LINK_COMPONENTS
  Trace
  )

add_llvm_unittest(TraceTests
  TraceTest.cpp
  )

target_link_libraries(TraceTests PRIVATE LLVMTestingSupport)
