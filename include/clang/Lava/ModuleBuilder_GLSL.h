//===--- ModuleBuilder_GLSL.h - Lava GLSL code generation -------*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_MODULEBUILDER_GLSL_H
#define LLVM_CLANG_LAVA_MODULEBUILDER_GLSL_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/Type.h"
#include "clang/Lava/GLSL.h"
#include "clang/lava/IndentWriter.h"

namespace clang
{
  class ASTContext;
  class MangleContext;

  namespace lava
  {
    class IndentWriter;
    class RecordBuilder;

    namespace glsl
    {
      class ModuleBuilder;
      class RecordBuilder;
      class TypeNamePrinter;
    }
  }
}

class clang::lava::glsl::TypeNamePrinter
{
public:
  TypeNamePrinter(ASTContext& ast)
  : _mangler(ItaniumMangleContext::create(ast, ast.getDiagnostics()))
  {
  }

  void printTypeName(QualType type, IndentWriter& w);
  void printCxxTypeName(QualType type, IndentWriter& w);

private:
  void printDimensionality(unsigned n, IndentWriter& w);

  std::unique_ptr<MangleContext> _mangler;
};

class clang::lava::glsl::RecordBuilder
{
public:
  RecordBuilder(QualType type, TypeNamePrinter& typeNamePrinter);

  bool addBase(QualType type, unsigned index);
  bool addField(QualType type, llvm::StringRef identifier);
  bool addCapture(QualType type, llvm::StringRef identifier);

  std::string finalize();

private:
  void printFieldImpl(QualType type, llvm::StringRef identifier);

  std::string _def;
  llvm::raw_string_ostream _ostream{_def};
  IndentWriter _w{_ostream};
  TypeNamePrinter& _typeNamePrinter;
  bool _printedBasesHeader : 1;
  bool _printedFieldsHeader : 1;
  bool _printedCapturesHeader : 1;
};

class clang::lava::glsl::ModuleBuilder
{
public:
  ModuleBuilder(ASTContext& ast);

  std::string moduleContent();

  template<class Director>
  bool buildRecord(QualType type, Director director);

private:
  // The definitions for all kinds of symbols are clustered together and we
  // simply expect them to get built in the correct order with no duplicates.
  // This allows us to control the order in which symbol types are printed so
  // we get the text-based dependencies right (define before use).

  struct
  {
    std::string defs;
  } _records;
  struct
  {
    std::string decls;
    std::string defs;
  } _functions;

  TypeNamePrinter _typeNamePrinter;
};

#endif // LLVM_CLANG_LAVA_MODULEBUILDER_GLSL_H
