//===--- SPIRV.h - ModuleBuilder factory for SPIR-V -------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_SPIRV_H
#define LLVM_CLANG_LAVA_SPIRV_H

namespace clang
{
  class ASTContext;

  namespace lava
  {
    class ModuleBuilder;

    namespace spirv
    {
      ModuleBuilder createModuleBuilder(ASTContext& ast);
    }
  }
}

#endif // LLVM_CLANG_LAVA_SPIRV_H
