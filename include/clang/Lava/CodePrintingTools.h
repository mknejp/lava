//===--- CodePrintingTools.h - Common utilities for text gen ----*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_CODEPRINTINGTOOLS_H
#define LLVM_CLANG_LAVA_CODEPRINTINGTOOLS_H

#include "clang/AST/Type.h"
#include <memory>
#include <string>

namespace clang
{
  class ASTContext;
  class FunctionDecl;
  class MangleContext;

  namespace lava
  {
    class IndentWriter;

    class TypeMangler
    {
    public:
      TypeMangler(ASTContext& ast);
      ~TypeMangler();

      void mangleCxxTypeName(QualType type, IndentWriter& w) const;
      void mangleCxxDeclName(NamedDecl& decl, IndentWriter& w) const;
      std::string mangleCxxTypeName(QualType type) const;
      std::string mangleCxxDeclName(NamedDecl& decl) const;

    private:
      std::unique_ptr<MangleContext> _mangler;
    };
    void printCxxFunctionName(FunctionDecl& decl, IndentWriter& w);
    void printCxxFunctionProto(FunctionDecl& decl, IndentWriter& w);
    std::string printCxxFunctionName(FunctionDecl& decl);
    std::string printCxxFunctionProto(FunctionDecl& decl);
  }
}

#endif // LLVM_CLANG_LAVA_CODEPRINTINGTOOLS_H
