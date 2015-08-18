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
#include <stack>

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
      struct BlockVariables;
      struct ExprResult;
      class FunctionBuilder;
      class LoopMergeContext;
      class LoopStack;
      class ModuleBuilder;
      class RecordBuilder;
      class StmtBuilder;
      class TypeCache;
      struct Variable;
      class VariablesStack;

      using Id = ::spv::Id;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// TypeCache
//

class clang::lava::spirv::TypeCache
{
public:
  TypeCache(spv::Builder& builder) : _builder(builder) { }

  void add(CXXRecordDecl* decl, Id id);
  Id get(QualType type) const;
  Id getPointer(QualType type, spv::StorageClass storage) const;
  Id operator[](QualType type) const { return get(type); }

  spv::Builder& builder() const { return _builder; }

private:
  // spv::Builder does not cache struct types in makeStructType()
  llvm::DenseMap<CXXRecordDecl*, Id> _builtRecords;
  spv::Builder& _builder;
};

////////////////////////////////////////////////////////////////////////////////
// RecordBuilder
//

class clang::lava::spirv::RecordBuilder
{
public:
  RecordBuilder(QualType type, TypeCache& types, ASTContext& ast);

  bool addBase(QualType type, unsigned index);
  bool addField(QualType type, llvm::StringRef identifier);
  bool addCapture(QualType type, llvm::StringRef identifier);
  Id finalize();

private:
  TypeCache& _types;
  ASTContext& _ast;
  std::vector<Id> _members;
  std::vector<std::string> _names;
  std::string _name;
  CXXRecordDecl* _decl;
};

////////////////////////////////////////////////////////////////////////////////
// Variable
//

// Tracks the state of every variable accessed in a function and is used to
// determine when load/store instructions must be inserted.
struct clang::lava::spirv::Variable
{
  // The id under which the frontend refers to this variable.
  const VarDecl* decl;
  // If this is != NoResult the variable is of pointer type
  Id pointer;
  // The Result<id> holding the most recent value of this variable that can be directly used in computations.
  // Can only be NoResult if pointer != NoResult, meaning the value must be loaded from the pointer before use.
  // NoResult is only valid for volatile pointers and pointer parameters which weren't loaded yet.
  // Once a variable has been loaded or stored this field can never get NoResult again (except for volatile).
  Id value;
  // Only relevant if pointer != NoResult
  spv::StorageClass storage;
  // True if loads/stores must not be removed. Only relevant if pointer != NoResult
  // Even thugh the decoration goes to a OpVariable we need to know whether to emit a load/store or not.
  bool isVolatile : 1;
  // Only used if pointer != NoResult.
  // True if there is a chance the value has changed since the most recent load.
  // This is used to figure out whether we need to store a variable before calling a function (in case the callee reads its value) or exiting the current function.
  bool isDirty : 1;
};

////////////////////////////////////////////////////////////////////////////////
// ExprResult
//

// Used to classify the return type of a SPIR-V expression and whether it is tied to a variable or access chain etc.
struct clang::lava::spirv::ExprResult
{
  // The Result<id> of the expression
  Id value;
  // Non-null if the result references a variable
  const VarDecl* variable;
  // If non-empty then the expression generated a member-access chain required for load/store or insert/extract.
  // Cannot use SmallVector because of spv::Builder interface
  std::vector<unsigned> chain;

  void reset()
  {
    value = spv::NoResult;
    variable = nullptr;
    chain.clear();
  }
};

////////////////////////////////////////////////////////////////////////////////
// BlockVariables
//

// Represents the state of all variables referenced in a block
struct clang::lava::spirv::BlockVariables
{
  std::vector<clang::lava::spirv::Variable> vars;
  spv::Block* block;
  LoopMergeContext* loop;
};

////////////////////////////////////////////////////////////////////////////////
// VariablesStack
//

class clang::lava::spirv::VariablesStack
{
public:
  class Merger;

  VariablesStack(TypeCache& types, spv::Builder& builder, spv::Block* block);

  auto initUndefined(const VarDecl* decl) -> Id;
  // *Must* be called with the top block set to the same block that contains
  // the instruction consuming this load!
  auto load(const ExprResult& expr) -> Id;
  // The given variable is currently being initialized.
  // If the variable is read we track it and assign it an OpUndef value.
  // It is the *only* variable which may be read if not being tracked.
  void markAsInitializing(const VarDecl& decl);
  auto store(const ExprResult& target, Id value) -> ExprResult;
  void setTopBlock(spv::Block* block);
  void trackVariable(const VarDecl& decl, Id id);

  // Extract all variables from the top and mark the remaining object as invalid
  // thus making it not participate in merges.
  auto extractTop() -> BlockVariables;
  void push(spv::Block* block, LoopMergeContext* loop);
  void pop() { _stack.pop_back(); }
  auto popAndGet() -> BlockVariables;

  // Merge the variables coming from control flow blocks and insert
  // OpPhi instructions at the beginning of the merge block ot stores in the
  // preceding blocks. When called the top variable block must be the one
  // immediately dominating all blocks in the given range.
  //
  // If "loop" is set then this merges "continue" blocks into a loop header
  // and variables are incorporated into the loop rewrite mechanic as necessary.
  template<class Iter>
  void merge(Iter first, Iter last, spv::Block* mergeBlock, LoopMergeContext* loop = nullptr);

private:
  static void sort(BlockVariables& blockVars);
  static void storeIfDirty(Variable& var, spv::Block* block);

  Id load(const VarDecl& decl);
  Variable& find(const VarDecl* decl);
  Variable* tryFind(const VarDecl* decl);
  Variable* tryFindInTop(const VarDecl* decl);
  Variable* tryFindInStackBelowTop(const VarDecl* decl);
  void storeIfDirty(Variable& var);
  BlockVariables& top() { return _stack.back(); }

  TypeCache& _types;
  spv::Builder& _builder;
  const VarDecl* _initing = nullptr;
  llvm::SmallVector<BlockVariables, 8> _stack;
};

////////////////////////////////////////////////////////////////////////////////
// LoopMergeContext
//

class clang::lava::spirv::LoopMergeContext
{
public:
  LoopMergeContext(spv::Builder& builder, LoopMergeContext* parent);

  void addBreakBlock(BlockVariables block);
  void addContinueBlock(BlockVariables block);
  void setHeaderBlock(BlockVariables block);
  //
  void addRewriteCandidate(const VarDecl* decl, Id operand, spv::Block* block);
  void setRewriteId(const VarDecl* decl, Id rewriteId);
  void applyRewrites();

  void mergeContinueBlocks(VariablesStack& vars, spv::Block* testBlock);
  void mergeBreakBlocks(VariablesStack& vars, spv::Block* mergeBlock);

  LoopMergeContext* parent() const { return _parent; }

private:
  struct VarInfo
  {
    const VarDecl* var;
    // TODO: It'd be great to only store the Instructions instead of the blocks,
    // but spv::Builder creates some instructions under the hood that we don't
    // have immediate access to. So as a temporary solution we have to remember
    // the blocks and then rewrite all their instructions consuming the old ID.
    llvm::SmallVector<spv::Block*, 4> blocks;
    Id tentativeId;
    Id rewriteId;
  };

  void rewriteInstruction(spv::Instruction* inst, Id oldId, Id newId);

  VarInfo* tryFind(const VarDecl* decl);
  void sort();

  llvm::SmallVector<VarInfo, 4> _rewriteCandidates;
  llvm::SmallVector<BlockVariables, 4> _breakBlocks;
  llvm::SmallVector<BlockVariables, 4> _continueBlocks;
  spv::Block* _preheader;
  BlockVariables _headerBlock;
  spv::Builder& _builder;
  LoopMergeContext* _parent;
};

////////////////////////////////////////////////////////////////////////////////
// LoopStack
//

class clang::lava::spirv::LoopStack
{
public:
  class ScopedPush;

  LoopStack() { push(nullptr); }

  void pop() { _stack.pop(); }
  void push(LoopMergeContext* ctx) { _stack.push(ctx); }
  auto top() -> LoopMergeContext* { return _stack.top(); }

private:
  std::stack<LoopMergeContext*, llvm::SmallVector<LoopMergeContext*, 4>> _stack;
};

////////////////////////////////////////////////////////////////////////////////
// StmtBuilder
//

class clang::lava::spirv::StmtBuilder
{
public:
  StmtBuilder(TypeCache& types, VariablesStack& variables);

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

  const ExprResult& expr() { return _subexpr; }

private:
  struct IncDecLiteral
  {
    Id id;
    bool floating;
    bool sign;
  };
  enum class IncDecOperator { inc, dec };

  static ExprResult makeRValue(Id value) { return {value, nullptr, {}}; }

  Id load(const ExprResult& expr) { return _vars.load(expr); }
  ExprResult store(const ExprResult& target, Id value) { return _vars.store(target, value); }
  ExprResult makePrefixOp(const ExprResult& lvalue, QualType type, IncDecOperator op);
  ExprResult makePostfixOp(const ExprResult& lvalue, QualType type, IncDecOperator op);
  IncDecLiteral makeLiteralForIncDec(QualType type);

  TypeCache& _types;
  spv::Builder& _builder;
  ExprResult _subexpr{}; // The result and classification of the most recent expression
  VariablesStack& _vars;
};

////////////////////////////////////////////////////////////////////////////////
// FunctionBuilder
//

class clang::lava::spirv::FunctionBuilder
{
public:
  FunctionBuilder(FunctionDecl& decl, TypeCache& types, TypeMangler& mangler);

  bool addParam(const ParmVarDecl& param);
  template<class F1, class F2>
  bool buildIfStmt(F1 condDirector, F2 thenDirector);
  template<class F1, class F2, class F3>
  bool buildIfStmt(F1 condDirector, F2 thenDirector, F3 elseDirector);
  template<class F>
  bool buildReturnStmt(F exprDirector);
  template<class F>
  bool buildStmt(F exprDirector);
  template<class F1, class F2>
  bool buildWhileStmt(F1 condDirector, F2 bodyDirector);
  bool declareUndefinedVar(const VarDecl& var);
  template<class F>
  bool declareVar(const VarDecl& var, F initDirector);
  template<class F>
  bool pushScope(F scopeDirector);
  bool setReturnType(QualType type);

  Id finalize();

private:
  Id load(const ExprResult& expr) { return _vars.load(expr); }
  ExprResult store(const ExprResult& target, Id value) { return _vars.store(target, value); }
  void trackParameter(const ParmVarDecl& param, Id id);

  TypeCache& _types;
  spv::Builder& _builder{_types.builder()};
  TypeMangler& _mangler;
  
  FunctionDecl& _decl;
  Id _returnType = spv::NoType;
  spv::Function* _function = nullptr;
  std::vector<const ParmVarDecl*> _params;
  VariablesStack _vars{_types, _builder, nullptr};
  LoopStack _loops;
};

////////////////////////////////////////////////////////////////////////////////
// ModuleBuilder
//

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
