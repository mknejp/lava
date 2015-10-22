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
#include "llvm/ADT/SmallBitVector.h"
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
      class  BreakStack;
      struct ExprResult;
      class  FunctionBuilder;
      class  LoopContext;
      struct MergeResult;
      class  ModuleBuilder;
      class  RecordBuilder;
      class  StmtBuilder;
      class  SwitchContext;
      class  TypeCache;
      struct Variable;
      class  VariablesStack;

      template<class Context>
      class ContextStack;

      using LoopStack = ContextStack<LoopContext>;
      using SwitchStack = ContextStack<SwitchContext>;

      using Id = ::spv::Id;

      constexpr bool operator<(const spirv::Variable& a, const spirv::Variable& b);
      constexpr bool operator<(const VarDecl* decl, const spirv::Variable& var);
      constexpr bool operator<(const spirv::Variable& var, const VarDecl* decl);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// TypeCache
//

class clang::lava::spirv::TypeCache
{
public:
  TypeCache(spv::Builder& builder) : _builder(&builder) { }

  void add(CXXRecordDecl* decl, Id id);
  Id get(QualType type) const;
  Id getPointer(QualType type, spv::StorageClass storage) const;
  Id operator[](QualType type) const { return get(type); }

  spv::Builder& builder() const { return *_builder; }

private:
  // spv::Builder does not cache struct types in makeStructType()
  llvm::DenseMap<CXXRecordDecl*, Id> _builtRecords;
  spv::Builder* _builder;
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
  LoopContext* loop;
  SwitchContext* switsh;
};

////////////////////////////////////////////////////////////////////////////////
// MergeResult
//

struct clang::lava::spirv::MergeResult
{
  struct Phi
  {
    Phi(Id value, spv::Block* block) : value(value), block(block) { }
    Id value;
    spv::Block* block;
  };
  struct Store
  {
    Store(Id pointer, Id value, spv::Block* block) : pointer(pointer), value(value), block(block) { }
    Id pointer;
    Id value;
    spv::Block* block;
  };
  struct MergedVariable
  {
    std::vector<Phi> phiBlocks;
    std::vector<Store> storeBlocks;
    Id phiResultId = spv::NoResult; // Only valid if !phiBlocks.empty() after applying the emrge
    bool isDirty;
  };

  std::map<const VarDecl*, MergedVariable> mergedVariables;
  spv::Block* mergeBlock;
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
  auto store(const ExprResult& target, const ExprResult& source) -> ExprResult;
  void setTopBlock(spv::Block* block);
  void trackVariable(const VarDecl& decl, Id id);

  // Extract all variables from the top and mark the remaining object as invalid
  // thus making it not participate in merges.
  auto extractTop() -> BlockVariables;
  void push(spv::Block* block, LoopContext* loop, SwitchContext* switsh);
  auto pop() -> BlockVariables;

  // Collapse all block variables associated to the current loop's variable
  // stack into the top. This is necessary for continue/break that leave
  // structured control flow so the top block has all the variable information
  // necessary for merging.
  void collapseLoopStackIntoTop();
  // Collapse all block variables associated to the current switch's variable
  // stack into the top. This is necessary for break that leave
  // structured control flow so the top block has all the variable information
  // necessary for merging.
  void collapseSwitchStackIntoTop();

  // Find the value of the variable *before* entering the given loop.
  Id findPreLoopValue(const VarDecl* decl, LoopContext& loop);

  // Analyse the control flow coming to the mergeBlock and buld up a
  // MergeResult structure containing information about how each active variable
  // should be merged.
  //
  // This does not emit any instructions.
  template<class Iter>
  MergeResult resolveMerge(Iter first, Iter last, spv::Block* mergeBlock);
  // Given a MergeResult object emit the instrucitons applying
  // the necessary changes in the associated merge block.
  //
  // If the merge result contains any phi resolutions store the
  // Result<id> generated for the associated OpPhi instruction in the object.
  void applyMergeResult(MergeResult& merge);
  void applyMergeResult(MergeResult&& merge) { return applyMergeResult(merge); }

private:
  using Stack = llvm::SmallVector<BlockVariables, 8>;
  static void sort(BlockVariables& blockVars);
  static bool shouldStoreIfDirty(const Variable& var);

  template<class F>
  void collapseStackIntoTop(F predicate);
  auto load(const VarDecl& decl) -> Id;
  auto find(const VarDecl* decl) -> Variable&;
  auto tryFind(const VarDecl* decl) -> Variable*;
  auto tryFindInTop(const VarDecl* decl) -> Variable*;
  auto tryFindInStackBelowTop(const VarDecl* decl) -> Variable*;
  auto tryFindInStack(Stack::reverse_iterator start, const VarDecl* decl) -> Variable*;
  auto top() -> BlockVariables& { return _stack.back(); }

  TypeCache& _types;
  spv::Builder& _builder;
  const VarDecl* _initing = nullptr;
  Stack _stack;
};

////////////////////////////////////////////////////////////////////////////////
// LoopContext
//

class clang::lava::spirv::LoopContext
{
public:
  template<class F>
  LoopContext(spv::Block* preheader, VariablesStack& vars, LoopContext* parent, F incDirector);

  void addBreakBlock(BlockVariables block);
  void addContinueBlock(BlockVariables block);
  void setHeaderBlock(BlockVariables block);
  void addRewriteCandidate(const VarDecl* decl, Id operand, spv::Block* block);

  void applyMerge(spv::Block* mergeBlock);

  LoopContext* parent() const { return _parent; }

  bool invokeIncDirector(FunctionBuilder& builder);

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

  void applyRewrites();
  void mergeContinueBlocks();
  void mergeBreakBlocks(spv::Block* mergeBlock);
  void rewriteInstruction(spv::Instruction* inst, Id oldId, Id newId);
  void setRewriteId(const VarDecl* decl, Id rewriteId);
  void sort();
  auto tryFind(const VarDecl* decl) -> VarInfo*;

  llvm::SmallVector<VarInfo, 4> _rewriteCandidates;
  llvm::SmallVector<BlockVariables, 4> _breakBlocks;
  llvm::SmallVector<BlockVariables, 4> _continueBlocks;
  spv::Block* _preheader;
  BlockVariables _headerBlock;
  LoopContext* _parent;
  VariablesStack& _vars;
  std::function<bool(FunctionBuilder&)> _incDirector;
};

////////////////////////////////////////////////////////////////////////////////
// SwitchContext
//

class clang::lava::spirv::SwitchContext
{
public:
  SwitchContext(spv::Block* headerBlock, spv::Block* mergeBlock, spv::Instruction* switchInst);

  void addCase(std::uint32_t value, spv::Block* block);
  void setDefault(spv::Block* block);
  bool hasDefault() const { return _defaultBlock != spv::NoResult; }
  void addBreakBlock(BlockVariables block);
  void mergeBreakBlocks(VariablesStack& vars);
  spv::Block* headerBlock() const { return _headerBlock; }
  spv::Block* mergeBlock() const { return _mergeBlock; }
  void rememberPendingMerge(BlockVariables predecessor);
  void applyPendingMerge(VariablesStack& vars, spv::Block* targetBlock);

private:
  spv::Block* _headerBlock;
  spv::Block* _mergeBlock;
  spv::Instruction* _switchInst;
  Id _defaultBlock = spv::NoResult;
  std::vector<BlockVariables> _breakBlocks;
  llvm::Optional<BlockVariables> _pendingMerge;
};

////////////////////////////////////////////////////////////////////////////////
// ContextStack
//

template<class Context>
class clang::lava::spirv::ContextStack
{
public:
  class PushScoped
  {
  public:
    PushScoped(ContextStack& stack, Context* ctx) : _stack(stack) { _stack.push(ctx); }
    ~PushScoped() { _stack.pop(); }

  private:
    ContextStack& _stack;
  };

  ContextStack() { push(nullptr); }

  void pop() { _stack.pop(); }
  void push(Context* ctx) { _stack.push(ctx); }
  auto top() -> Context* { return _stack.top(); }

private:
  std::stack<Context*, llvm::SmallVector<Context*, 4>> _stack;
};

////////////////////////////////////////////////////////////////////////////////
// BreakStack
//

class clang::lava::spirv::BreakStack
{
public:
  class PushLoopScoped;
  class PushSwitchScoped;

  BreakStack() { _stack.reserve(8); } // Very unlikely to have such deep loop/switch nesting
  BreakStack(const BreakStack&) = delete;
  BreakStack& operator=(const BreakStack&) = delete;

  bool isLoop() const { return !_stack.empty() && _stack[_stack.size() - 1] == loopBreak; }
  bool isSwitch() const { return !_stack.empty() && _stack[_stack.size() - 1] == switchBreak; }

private:
  static constexpr auto loopBreak = true;
  static constexpr auto switchBreak = false;

  void push(bool value) { _stack.resize(_stack.size() + 1, value); }
  void pop() { _stack.resize(_stack.size()  -1); }

  llvm::SmallBitVector _stack;
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
  ExprResult store(const ExprResult& target, const ExprResult& source) { return _vars.store(target, source); }
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

  Id finalize();

private:
  void applyPendingSwitchMerge();
  template<class F1, class F2>
  bool buildSimpleLoopCommon(bool testFirst, F1 condDirector, F2 bodyDirector);
  Id load(const ExprResult& expr) { return _vars.load(expr); }
  void nextSwitchCaseBlock();
  BlockVariables popBlockVars() { return _vars.pop(); }
  void pushBlockVars() { pushBlockVars(_builder.getBuildPoint()); }
  void pushBlockVars(spv::Block* block) { _vars.push(block, _loops.top(), _switches.top()); }
  ExprResult store(const ExprResult& target, Id value) { return _vars.store(target, value); }
  ExprResult store(const ExprResult& target, const ExprResult& source) { return _vars.store(target, source); }
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
  SwitchStack _switches;
  BreakStack _breaks;
};

////////////////////////////////////////////////////////////////////////////////
// ModuleBuilder
//

class clang::lava::spirv::ModuleBuilder
{
public:
  ModuleBuilder(ASTContext& ast, Options opts);
  
  std::string reset();

  template<class Director>
  bool buildRecord(QualType type, Director director);
  template<class Director>
  bool buildFunction(FunctionDecl& decl, Director director);

private:
  ASTContext* _ast;
  spv::Builder _builder{0};
  TypeCache _types{_builder};
  TypeMangler _mangler;
  Options _opts;
};

#endif // LLVM_CLANG_LAVA_MODULEBUILDER_SPIRV_H
