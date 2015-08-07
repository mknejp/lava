set(LLVM_LINK_COMPONENTS support)

include_directories(${CMAKE_CURRENT_LIST_DIR}/../../utils/glslang)

add_clang_library(clangLava
  LavaAction.cpp
  ModuleBuilder.cpp
  GLSL.cpp
  ShaderContext.cpp
  SPIRV.cpp

  LINK_LIBS
  clangSema
  SPIRV
  )
