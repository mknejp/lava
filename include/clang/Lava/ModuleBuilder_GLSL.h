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
      class FunctionBuilder;
      class ModuleBuilder;
      class RecordBuilder;
      class StmtBuilder;
      class TypeNamePrinter;
    }
  }
}

class clang::lava::glsl::TypeNamePrinter
{
public:
  TypeNamePrinter(ASTContext& ast);
  TypeNamePrinter(TypeNamePrinter&&);
  TypeNamePrinter& operator=(TypeNamePrinter&&);
  ~TypeNamePrinter();

  void printTypeName(QualType type, IndentWriter& w);
  void printFunctionName(FunctionDecl& decl, IndentWriter& w);
  void printCxxTypeName(QualType type, IndentWriter& w);
  void printCxxFunctionName(FunctionDecl& decl, IndentWriter& w);

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

class clang::lava::glsl::StmtBuilder
{
public:
  StmtBuilder(TypeNamePrinter& typeNamePrinter, IndentWriter& w);

  template<class RHS, class LHS>
  bool emitBinaryOperator(const BinaryOperator& expr, RHS lhs, LHS rhs);
  bool emitBooleanLiteral(const CXXBoolLiteralExpr& expr);
  template<class F>
  bool emitCast(const CastExpr& expr, F subexpr);
  bool emitFloatingLiteral(const FloatingLiteral& expr);
  bool emitIntegerLiteral(const IntegerLiteral& expr);
  template<class F>
  bool emitParenExpr(F subexpr);
  template<class F>
  bool emitUnaryOperator(const UnaryOperator& expr, F subexpr);
  bool emitVariableAccess(const VarDecl& var);

private:
  TypeNamePrinter& _typeNamePrinter;
  IndentWriter& _w;
};

class clang::lava::glsl::FunctionBuilder
{
public:
  FunctionBuilder(FunctionDecl& decl, TypeNamePrinter& typeNamePrinter);

  bool addParam(const ParmVarDecl& param);
  template<class F>
  bool buildBreakStmt(F&& cleanupDirector);
  template<class F>
  bool buildContinueStmt(F&& cleanupDirector);
  template<class F1, class F2>
  bool buildDoStmt(F1 condDirector, F2 bodyDirector);
  template<class F1, class F2, class F3, class F4>
  bool buildForStmt(bool hasCond, F1 initDirector, F2 condDirector,
                    F3 incDirector, F4 bodyDirector);
  template<class F1, class F2>
  bool buildIfStmt(F1 condDirector, F2 thenDirector);
  template<class F1, class F2, class F3>
  bool buildIfStmt(F1 condDirector, F2 thenDirector, F3 elseDirector);
  template<class F>
  bool buildReturnStmt(F exprDirector);
  template<class F>
  bool buildStmt(F exprDirector);
  template<class F1, class F2>
  bool buildSwitchStmt(F1 condDirector, F2 bodyDirector);
  bool buildSwitchCaseStmt(llvm::APSInt value);
  bool buildSwitchDefaultStmt();
  template<class F1, class F2>
  bool buildWhileStmt(F1 condDirector, F2 bodyDirector);
  bool declareUndefinedVar(const VarDecl& var);
  template<class F>
  bool declareVar(const VarDecl& var, F initDirector);
  template<class F>
  bool pushScope(F scopeDirector);
  bool setReturnType(QualType type);

  void finalize();

  std::string declaration() { return std::move(_declString); }
  std::string definition() { return std::move(_defString); }

private:
  void buildProtoStrings();
  void emitVarInitPrefix(const VarDecl& var);
  void emitVarInitSuffix();

  std::string _declString;
  std::string _defString;
  llvm::raw_string_ostream _ostream{_defString};
  IndentWriter _w{_ostream};
  TypeNamePrinter& _typeNamePrinter;

  FunctionDecl& _decl;
  QualType _returnType;
  std::vector<const ParmVarDecl*> _formalParams;
  int _forLoopInitializer = 0;
};

class clang::lava::glsl::ModuleBuilder
{
public:
  ModuleBuilder(ASTContext& ast);

  std::string reset();

  template<class Director>
  bool buildRecord(QualType type, Director director);
  template<class Director>
  bool buildFunction(FunctionDecl& decl, Director director);

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
  ASTContext* _ast;
};

#endif // LLVM_CLANG_LAVA_MODULEBUILDER_GLSL_H
