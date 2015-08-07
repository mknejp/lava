set( LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  Analysis
  CodeGen
  Core
  IPA
  IPO
  InstCombine
  Instrumentation
  MC
  MCParser
  ObjCARCOpts
  Option
  ScalarOpts
  Support
  TransformUtils
  Vectorize
  )

add_clang_executable(lava
  Lava.cpp
  Version.h
  )

target_link_libraries(lava
  clangBasic
  clangDriver
  clangFrontend
  clangFrontendTool
  clangLava
  clangTooling
  )
