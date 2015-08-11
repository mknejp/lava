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
  class ParmVarDecl;
  class VarDecl;
  
  namespace lava
  {
    class RecordBuilder;
    
    namespace spirv
    {
      struct ExprResult;
      class FunctionBuilder;
      class ModuleBuilder;
      class RecordBuilder;
      class StmtBuilder;
      class TypeCache;
      struct Variable;
      class Variables;
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

// Tracks the state of every variable accessed in a function and is used to
// determine when load/store instructions must be inserted.
struct clang::lava::spirv::Variable
{
  // The id under which the frontend refers to this variable.
  const VarDecl* var;
  // If this is != 0 the variable is of pointer type
  spv::Id pointer;
  // The Result<id> holding the most recent value of this variable that can be directly used in computations.
  // Can only be 0 if pointer != 0, meaning the value must be loaded from the pointer before use.
  spv::Id value;
  // Only relevant if pointer != 0
  spv::StorageClass storage;
  // True if loads/stores must not be removed. Only relevant if pointer != 0
  // Even thugh the decoration goes to a OpVariable we need to know whether to emit a load/store or not.
  bool isVolatile : 1;
  // Only used if pointer != 0.
  // True if there is a chance the value has changed since the most recent load.
  // This is used to figure out whether we need to store a variable before calling a function (in case the callee reads its value) or exiting the current function.
  bool isDirty : 1;
  // True if the variable is initialized.
  // Trying to load it with inited == false produces an undefined value.
  bool inited : 1;
};

// Used to classify the return type of a SPIR-V expression and whether it is tied to a variable or access chain etc.
struct clang::lava::spirv::ExprResult
{
  // The Result<id> of the expression
  spv::Id value;
  // Non-null if the result references a variable
  const VarDecl* variable;
  // If non-empty then the expression generated a member-access chain required for load/store or insert/extract.
  // Cannot use SmallVector because of spv::Builder interface
  std::vector<unsigned> chain;

  void reset()
  {
    value = 0;
    variable = nullptr;
    chain.clear();
  }
};

// Track the state of all variables in a function
class clang::lava::spirv::Variables
{
public:
  Variables(TypeCache& types);
  Variables(const Variables&) = delete;
  Variables& operator=(const Variables&) = delete;

  spv::Id load(const VarDecl& decl);
  spv::Id load(const ExprResult& expr);
  ExprResult store(const ExprResult& target, spv::Id value);
  void trackUndefVariable(const VarDecl& decl);
  // Set id = 0 to indicate a local uninitialized variable
  void trackVariable(const VarDecl& decl, spv::Id id);

private:
  Variable& find(const VarDecl* decl);

  TypeCache& _types;
  spv::Builder& _builder;
  std::vector<Variable> _vars;
};

class clang::lava::spirv::StmtBuilder
{
public:
  StmtBuilder(TypeCache& types, Variables& variables);

  template<class RHS, class LHS>
  bool emitBinaryOperator(const BinaryOperator& expr, RHS lhs, LHS rhs);
  bool emitBooleanLiteral(const CXXBoolLiteralExpr& expr);
  bool emitFloatingLiteral(const FloatingLiteral& expr);
  bool emitIntegerLiteral(const IntegerLiteral& expr);
  template<class F>
  bool emitParenExpr(F subexpr);
  template<class F>
  bool emitUnaryOperator(const UnaryOperator& expr, F subexpr);

  const ExprResult& expr() { return _subexpr; }

private:
  struct IncDecLiteral
  {
    spv::Id id;
    bool floating;
    bool sign;
  };
  enum class IncDecOperator { inc, dec };

  static ExprResult makeRValue(spv::Id value) { return {value, nullptr, {}}; }

    spv::Id load(const VarDecl& decl) { return _vars.load(decl); }
  spv::Id load(const ExprResult& expr) { return _vars.load(expr); }
  ExprResult store(const ExprResult& target, spv::Id value) { return _vars.store(target, value); }
  ExprResult makePrefixOp(const ExprResult& lvalue, QualType type, IncDecOperator op);
  ExprResult makePostfixOp(const ExprResult& lvalue, QualType type, IncDecOperator op);
  IncDecLiteral makeLiteralForIncDec(QualType type);

  TypeCache& _types;
  spv::Builder& _builder;
  ExprResult _subexpr{}; // The result and classification of the most recent expression
  Variables& _vars;
};

class clang::lava::spirv::FunctionBuilder
{
public:
  FunctionBuilder(FunctionDecl& decl, TypeCache& types, TypeMangler& mangler);

  bool addParam(const ParmVarDecl& param);
  template<class F>
  bool buildReturnStmt(F exprDirector);
  template<class F>
  bool buildStmt(F exprDirector);
  bool declareUndefinedVar(const VarDecl& var);
  template<class F>
  bool declareVar(const VarDecl& var, F initDirector);
  template<class F>
  bool pushScope(F scopeDirector);
  bool setReturnType(QualType type);

  spv::Id finalize();

private:
  void trackParameter(const ParmVarDecl& param, spv::Id id);

  TypeCache& _types;
  spv::Builder& _builder{_types.builder()};
  TypeMangler& _mangler;
  
  FunctionDecl& _decl;
  spv::Id _returnType = 0;
  spv::Function* _function = nullptr;
  std::vector<const ParmVarDecl*> _params;
  Variables _variables{_types};
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
