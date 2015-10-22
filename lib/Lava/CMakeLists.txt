set(LLVM_LINK_COMPONENTS support)

include_directories(${CMAKE_CURRENT_LIST_DIR}/../../utils/glslang)

add_clang_library(clangLava
  CodeGen.cpp
  CodePrintingTools.cpp
  GLSL.cpp
  LavaAction.cpp
  ModuleBuilder.cpp
  ShaderContext.cpp
  SPIRV.cpp
  Target.cpp
  Target_GLSL.cpp
  Target_SPIRV.cpp

  LINK_LIBS
  clangSema
  SPIRV
  )
