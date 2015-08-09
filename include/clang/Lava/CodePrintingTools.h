//===--- CodePrintingTools.h - Common utilities for text gen ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Many of the languages we generate code for share many aspects. This file
// defines some utilities to cover as much common stuff as possible.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LAVA_CODEPRINTINGTOOLS_H
#define LLVM_CLANG_LAVA_CODEPRINTINGTOOLS_H

#include "clang/AST/Type.h"
#include "clang/AST/OperationKinds.h"
#include <memory>
#include <string>

namespace clang
{
  class ASTContext;
  class FloatingLiteral;
  class FunctionDecl;
  class MangleContext;

  namespace lava
  {
    class IndentWriter;

    enum PrintFloatingSuffix
    {
      PrintFloatingSuffixNone,
      PrintFloatingSuffixHalf       = 1 << 0,
      PrintFloatingSuffixFloat      = 1 << 1,
      PrintFloatingSuffixDouble     = 1 << 2,
      PrintFloatingSuffixLongDouble = 1 << 3,
    };

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

    void printOperator(BinaryOperatorKind opcode, IndentWriter w);
    void printOperator(UnaryOperatorKind opcode, IndentWriter w);
    void printBoolLiteral(bool value, IndentWriter& w);
    void printFloatingLiteral(const FloatingLiteral& literal, PrintFloatingSuffix suffix, IndentWriter& w);
  }
}

#endif // LLVM_CLANG_LAVA_CODEPRINTINGTOOLS_H
