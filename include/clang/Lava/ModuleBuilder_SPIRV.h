//===--- ModuleBuilder_SPIRV.h - Lava SPIR-V code generation ----*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_MODULEBUILDER_SPIRV_H
#define LLVM_CLANG_LAVA_MODULEBUILDER_SPIRV_H

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "clang/Lava/CodePrintingTools.h"
#include "clang/Lava/SPIRV.h"
#include "SPIRV/SpvBuilder.h"

namespace clang
{
  class CXXBoolLiteralExpr;
  
  namespace lava
  {
    class RecordBuilder;
    
    namespace spirv
    {
      class FunctionBuilder;
      class ModuleBuilder;
      class RecordBuilder;
      class StmtBuilder;
      class TypeCache;
    }
  }
}

class clang::lava::spirv::TypeCache
{
public:
  TypeCache(spv::Builder& builder) : _builder(builder) { }

  void add(CXXRecordDecl* decl, spv::Id id);
  spv::Id get(QualType type) const;
  spv::Id getPointer(QualType type, spv::StorageClass storage) const;
  spv::Id operator[](QualType type) const { return get(type); }

  spv::Builder& builder() const { return _builder; }

private:
  // spv::Builder does not cache struct types in makeStructType()
  llvm::DenseMap<CXXRecordDecl*, spv::Id> _builtRecords;
  spv::Builder& _builder;
};

class clang::lava::spirv::RecordBuilder
{
public:
  RecordBuilder(QualType type, TypeCache& types, ASTContext& ast);

  bool addBase(QualType type, unsigned index);
  bool addField(QualType type, llvm::StringRef identifier);
  bool addCapture(QualType type, llvm::StringRef identifier);
  spv::Id finalize();

private:
  TypeCache& _types;
  ASTContext& _ast;
  std::vector<spv::Id> _members;
  std::vector<std::string> _names;
  std::string _name;
  CXXRecordDecl* _decl;
};

class clang::lava::spirv::StmtBuilder
{
public:
  template<class RHS, class LHS>
  bool emitBinaryOperator(const BinaryOperator& expr, RHS lhs, LHS rhs) { return true; }
  bool emitBooleanLiteral(const CXXBoolLiteralExpr& expr) { return true; }
  bool emitFloatingLiteral(const FloatingLiteral& expr) { return true; }
  bool emitIntegerLiteral(const IntegerLiteral& literal) { return true; }
  template<class F>
  bool emitParenExpr(F subexpr);
  template<class F>
  bool emitUnaryOperator(const UnaryOperator& expr, F director) { return true; }

private:
};
class clang::lava::spirv::FunctionBuilder
{
public:
  FunctionBuilder(FunctionDecl& decl, TypeCache& types, TypeMangler& mangler);

  bool addParam(const ParmVarDecl& param);

  template<class F>
  bool buildStmt(F director);

  bool declareUndefinedVar(const VarDecl& var);

  template<class F>
  bool declareVar(const VarDecl& var, F director);

  template<class F>
  bool pushScope(F director);

  bool setReturnType(QualType type);

  spv::Id finalize();

private:
  TypeCache& _types;
  spv::Builder& _builder{_types.builder()};
  TypeMangler& _mangler;
  
  FunctionDecl& _decl;
  spv::Id _returnType = 0;
  spv::Function* _function = nullptr;
  std::vector<const ParmVarDecl*> _params;
};

class clang::lava::spirv::ModuleBuilder
{
public:
  ModuleBuilder(ASTContext& ast);

  std::string moduleContent();

  template<class Director>
  bool buildRecord(QualType type, Director director);
  template<class Director>
  bool buildFunction(FunctionDecl& decl, Director director);

private:
  ASTContext& _ast;
  spv::Builder _builder{0};
  TypeCache _types{_builder};
  TypeMangler _mangler;
};

#endif // LLVM_CLANG_LAVA_MODULEBUILDER_SPIRV_H
