//===--- CodePrintingTools.cpp - Common utilities for text gen --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Lava/CodePrintingTools.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/Lava/IndentWriter.h"

using namespace clang;
using namespace lava;

namespace
{
  template<class F>
  std::string stringFromIndentWriter(F f)
  {
    std::string result;
    {
      llvm::raw_string_ostream out{result};
      IndentWriter w{out};
      f(w);
    }
    return result;
  }
}

TypeMangler::TypeMangler(ASTContext& ast)
: _mangler(ItaniumMangleContext::create(ast, ast.getDiagnostics()))
{
}

TypeMangler::~TypeMangler() = default;

void TypeMangler::mangleCxxTypeName(QualType type, IndentWriter& w) const
{
  _mangler->mangleTypeName(type, w.ostreamWithIndent());
}

void TypeMangler::mangleCxxDeclName(NamedDecl& decl, IndentWriter& w) const
{
  // TODO: ctro/dtor must be treated separately
  _mangler->mangleCXXName(&decl, w.ostreamWithIndent());
}

std::string clang::lava::TypeMangler::mangleCxxTypeName(QualType type) const
{
  return stringFromIndentWriter([&type, this] (IndentWriter& w)
  {
    mangleCxxTypeName(type, w);
  });
}

std::string clang::lava::TypeMangler::mangleCxxDeclName(NamedDecl& decl) const
{
  return stringFromIndentWriter([&decl, this] (IndentWriter& w)
  {
    mangleCxxDeclName(decl, w);
  });
}

void clang::lava::printCxxFunctionName(FunctionDecl& decl, IndentWriter& w)
{
  decl.printQualifiedName(w.ostreamWithIndent());
}

void clang::lava::printCxxFunctionProto(FunctionDecl& decl, IndentWriter& w)
{
  auto& policy = decl.getASTContext().getPrintingPolicy();
  decl.getReturnType().print(w.ostreamWithIndent(), policy);
  w << ' ';
  decl.printQualifiedName(w.ostream(), policy);
  w << '(';
  auto first = true;
  for(const auto& param: decl.params())
  {
    if(!first)
      w << ", ";
    first = false;
    param->print(w.ostream(), policy);
  }
  w << ')';
}

std::string clang::lava::printCxxFunctionName(FunctionDecl& decl)
{
  return stringFromIndentWriter([&decl] (IndentWriter& w)
  {
    printCxxFunctionName(decl, w);
  });
}

std::string clang::lava::printCxxFunctionProto(FunctionDecl& decl)
{
  return stringFromIndentWriter([&decl] (IndentWriter& w)
  {
    printCxxFunctionProto(decl, w);
  });
}
