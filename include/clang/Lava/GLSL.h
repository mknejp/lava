//===--- GLSL.h - ModuleBuilder factory for GLSL ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LAVA_GLSL_H
#define LLVM_CLANG_LAVA_GLSL_H

namespace clang
{
  class ASTContext;

  namespace lava
  {
    class ModuleBuilder;

    namespace glsl
    {
      ModuleBuilder createModuleBuilder(ASTContext& ast);
    }
  }
}

#endif // LLVM_CLANG_LAVA_GLSL_H
