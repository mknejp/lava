//===--- SPIRV.cpp - SPIR-V Code Gen ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Lava/ModuleBuilder_SPIRV.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Lava/ModuleBuilder.h"
#include "clang/Lava/IndentWriter.h"
#include <set>

using namespace clang;
using namespace lava;

using spirv::Id;
using llvm::StringRef;

ModuleBuilder clang::lava::spirv::createModuleBuilder(ASTContext& ast)
{
  return lava::ModuleBuilder::create<spirv::ModuleBuilder>(ast);
}

namespace
{
  spv::Op opcode(BinaryOperatorKind op, bool floating, bool sign)
  {
    using namespace spv;
    // For arithmetic assignment we only produce the operation here.
    // The assignment needs to be handled separately.
    switch(op)
    {
      case BO_PtrMemD:
      case BO_PtrMemI:
        llvm_unreachable("pointer-to-member not supported");
      case BO_MulAssign:
      case BO_Mul:       return floating ? OpFMul : OpIMul;
      case BO_DivAssign:
      case BO_Div:       return floating ? OpFDiv : (sign ? OpSDiv : OpUDiv);
      case BO_RemAssign:
      case BO_Rem:       return floating ? OpFMod : (sign ? OpSMod : OpUMod);
      case BO_AddAssign:
      case BO_Add:       return floating ? OpFAdd : OpIAdd;
      case BO_SubAssign:
      case BO_Sub:       return floating ? OpFSub : OpISub;
      case BO_ShlAssign:
      case BO_Shl:       return sign ? OpShiftRightArithmetic : OpShiftLeftLogical;
      case BO_ShrAssign:
      case BO_Shr:       return sign ? OpShiftRightArithmetic : OpShiftRightLogical;
      case BO_LT:        return floating ? OpFOrdLessThan : (sign ? OpSLessThan : OpULessThan);
      case BO_GT:        return floating ? OpFOrdGreaterThan : (sign ? OpSGreaterThan : OpUGreaterThan);
      case BO_LE:        return floating ? OpFOrdLessThanEqual : (sign ? OpSLessThanEqual : OpULessThanEqual);
      case BO_GE:        return floating ? OpFOrdGreaterThanEqual : (sign ? OpSGreaterThanEqual : OpUGreaterThanEqual);
      case BO_EQ:        return floating ? OpFOrdEqual : OpIEqual;
      case BO_NE:        return floating ? OpFOrdNotEqual : OpINotEqual;
      case BO_AndAssign:
      case BO_And:       return OpBitwiseAnd;
      case BO_XorAssign:
      case BO_Xor:       return OpBitwiseXor;
      case BO_OrAssign:
      case BO_Or:        return OpBitwiseOr;
      case BO_LAnd:      return OpLogicalAnd;
      case BO_LOr:       return OpLogicalOr;
      case BO_Assign:
        llvm_unreachable("assignment operator should be special cased");
      case BO_Comma:
        llvm_unreachable("comma operator should be special cased");
    }
  }
  spv::Op opcode(UnaryOperatorKind op, bool floating, bool sign)
  {
    using namespace spv;
    switch(op)
    {
      case UO_Minus:   return floating ? OpFNegate : OpSNegate;
      case UO_Not:     return OpNot;
      case UO_LNot:    return OpLogicalNot;
      case UO_PostInc:
      case UO_PostDec:
      case UO_PreInc:
      case UO_PreDec:
        llvm_unreachable("inc/dec should have been special cased");
      case UO_Plus:
        llvm_unreachable("unaroy plus should have been special cased");
      case UO_AddrOf:
      case UO_Deref:
      case UO_Real:
      case UO_Imag:
      case UO_Extension:
        llvm_unreachable("operator not supported");
    }
  }
  bool isConstOrSpec(const spv::Builder& builder, Id value)
  {
    switch(builder.getOpCode(value))
    {
      case spv::OpConstant:
      case spv::OpConstantComposite:
      case spv::OpConstantFalse:
      case spv::OpConstantNull:
      case spv::OpConstantSampler:
      case spv::OpConstantTrue:
      case spv::OpSpecConstant:
      case spv::OpSpecConstantComposite:
      case spv::OpSpecConstantFalse:
      case spv::OpSpecConstantTrue:
        return true;
        
      default:
        return false;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// TypeCache
//

Id spirv::TypeCache::get(QualType type) const
{
  if(auto* arr = llvm::dyn_cast_or_null<ConstantArrayType>(type->getAsArrayTypeUnsafe()))
  {
    auto n = arr->getSize().getZExtValue();
    assert(n <= std::numeric_limits<unsigned>::max());
    return _builder.makeArrayType(get(arr->getElementType()),
                                 static_cast<unsigned>(n));
  }
  else if(auto* ref = type->getAs<ReferenceType>())
  {
    // This function never produces a pointer type
    return get(ref->getPointeeType());
  }
  else if(auto* builtin = type->getAs<BuiltinType>())
  {
    switch(builtin->getKind())
    {
      case BuiltinType::Kind::Void:
        return _builder.makeVoidType();
      case BuiltinType::Kind::Bool:
        return _builder.makeBoolType();
      case BuiltinType::Kind::Int:
        return _builder.makeIntType(32);
      case BuiltinType::Kind::UInt:
        return _builder.makeUintType(32);
      case BuiltinType::Kind::Long:
      case BuiltinType::Kind::LongLong:
        return _builder.makeIntType(64);
      case BuiltinType::Kind::ULong:
      case BuiltinType::Kind::ULongLong:
        return _builder.makeUintType(64);
      case BuiltinType::Kind::Half:
        return _builder.makeFloatType(16);
      case BuiltinType::Kind::Float:
        return _builder.makeFloatType(32);
      case BuiltinType::Kind::Double:
      case BuiltinType::Kind::LongDouble:
        return _builder.makeFloatType(64);
      default:
        llvm_unreachable("TODO: other builtin types not implemented");
    }
  }
  else if(auto* vec = type->getAs<ExtVectorType>())
  {
    return _builder.makeVectorType(get(vec->getElementType()),
                                   vec->getNumElements());
  }
  else if(auto* mat = type->getAs<MatrixType>())
  {
    return _builder.makeMatrixType(get(mat->getElementType()),
                                   mat->getNumColumns(), mat->getNumRows());
  }
  else if(auto* record = type->getAsCXXRecordDecl())
  {
    // All records must be created in dependency order, so it must already exist
    auto id = _builtRecords.find(record);
    assert(id != _builtRecords.end() && "record types must be defined before use");
    return id->second;
  }
  llvm_unreachable("type not supported/implemented for SPIR-V");
}

Id spirv::TypeCache::getPointer(QualType type, spv::StorageClass storage) const
{
  return _builder.makePointer(storage, get(type));
}

void spirv::TypeCache::add(CXXRecordDecl* decl, Id id)
{
  assert(_builtRecords.find(decl) == _builtRecords.end()
         && "Records must be added at most once");
  _builtRecords[decl] = id;
}

////////////////////////////////////////////////////////////////////////////////
// RecordBuilder
//

spirv::RecordBuilder::RecordBuilder(QualType type, TypeCache& types, ASTContext& ast)
: _types(types)
, _ast(ast)
, _decl(type->getAsCXXRecordDecl())
{
  llvm::raw_string_ostream out{_name};
  type.print(out, _ast.getPrintingPolicy());
}

bool spirv::RecordBuilder::addBase(QualType type, unsigned index)
{
  std::string name;
  {
    llvm::raw_string_ostream out{name};
    out << "base$" << index;
  }
  _members.emplace_back(_types[type]);
  _names.emplace_back(std::move(name));
  return true;
}

bool spirv::RecordBuilder::addField(QualType type, llvm::StringRef identifier)
{
  _members.emplace_back(_types[type]);
  _names.emplace_back(identifier.str());
  return true;
}

bool spirv::RecordBuilder::addCapture(QualType type, llvm::StringRef identifier)
{
  _members.emplace_back(_types[type]);
  _names.emplace_back(identifier.str());
  return true;
}

Id spirv::RecordBuilder::finalize()
{
  auto id = _types.builder().makeStructType(_members, _name.c_str());

  auto i = 0;
  for(const auto& name : _names)
  {
    _types.builder().addMemberName(id, i++, name.c_str());
  }
  return id;
}

////////////////////////////////////////////////////////////////////////////////
// Variable
//

constexpr bool spirv::operator<(const spirv::Variable& a, const spirv::Variable& b)
{
  return a.decl < b.decl;
}
constexpr bool spirv::operator<(const VarDecl* decl, const spirv::Variable& var)
{
  return decl < var.decl;
}
constexpr bool spirv::operator<(const spirv::Variable& var, const VarDecl* decl)
{
  return var.decl < decl;
}

////////////////////////////////////////////////////////////////////////////////
// BlockVariables
//

namespace
{
  spirv::Variable* tryFind(const VarDecl* decl, spirv::BlockVariables& blockVars)
  {
    using namespace spirv;
    struct Comp
    {
      bool operator()(const VarDecl* decl, const Variable& var) const { return decl < var; }
      bool operator()(const Variable& var, const VarDecl* decl) const { return var < decl; }
    };
    auto it = std::lower_bound(blockVars.vars.begin(), blockVars.vars.end(), decl, Comp{});
    return it == blockVars.vars.end() ? nullptr : (it->decl == decl ? &*it : nullptr);
  }
}

////////////////////////////////////////////////////////////////////////////////
// VariablesStack::Merger
//

class spirv::VariablesStack::Merger
{
public:
  template<class Iter>
  Merger(VariablesStack& self, Iter first, Iter last, spv::Block* mergeBlock);

  MergeResult result() { return std::move(_result); }

private:
  struct Var
  {
    Variable variable;
    spv::Block* block;
  };
  using Grouped = std::map<const VarDecl*, std::vector<Var>>;

  void processVariable(Grouped::value_type& kvp, VariablesStack& self);
  template<class Iter>
  void groupByVarDecl(Iter first, Iter last);
  void resolveAsStore(VariablesStack& self, const Grouped::value_type& group);
  void resolveAsPhi(Variable& myVar, VariablesStack& self, const Grouped::value_type& group);

  Grouped _grouped;
  llvm::SmallVector<spv::Block*, 8> _blocks;
  MergeResult _result;
};

template<class Iter>
spirv::VariablesStack::Merger::Merger(VariablesStack& self, Iter first, Iter last,
                                      spv::Block* mergeBlock)
{
  _result.mergeBlock = mergeBlock;
  groupByVarDecl(std::move(first), std::move(last));
  for(auto& kvp : _grouped)
  {
    processVariable(kvp, self);
  }
}

void spirv::VariablesStack::Merger::processVariable(Grouped::value_type& group,
                                                    VariablesStack& self)
{
  Variable* myVar = group.first->hasLocalStorage() ? &self.find(group.first) : self.tryFind(group.first);
  if(!myVar)
  {
    // The dominating blocks never loaded this global variable.
    // Emit stores in all blocks which loaded it so regardless what block the
    // merge is reached from the next load gets the same value every time.
    // There is no need for a loop rewrite as the variable was not present when
    // the loop started.

    // TODO: if the domiating block is not a predecessor of the merge then
    // alternatively we might balance out load/stores according to load/store
    // ratios in the branches.
    resolveAsStore(self, group);
  }
  else
  {
    auto modified = std::any_of(group.second.begin(), group.second.end(), [myVar] (const Var& var)
                                {
                                  return var.variable.value != myVar->value;
                                });
    if(!modified)
    {
      // Case 1: Either the value was not modified, or it is volatile.
      // Nothing to do here.
    }
    else if(myVar->value == spv::NoResult)
    {
      // Case 2: The dominating blocks never loaded this pointer parameter.
      // Emit stores in all blocks which loaded it so regardless what block the
      // merge is reached from the next load gets the same value every time.
      // There is no need for a loop rewrite as the variable was not present
      // when the loop started.

      // TODO: if the domiating block is not a predecessor of the merge then
      // alternatively we might balance out load/stores according to load/store
      // ratios in the branches.
      assert(myVar->decl->hasLocalStorage() && myVar->pointer != spv::NoResult && "not a local pointer?");
      resolveAsStore(self, group);
    }
    else
    {
      // Case 3: Some block modified the variable
      // Collect all the participating values. If a block modified the variable
      // insert the new value into the result, for all the others take the value
      // from the dominating block.
      resolveAsPhi(*myVar, self, group);
    }
  }
}

void spirv::VariablesStack::Merger::resolveAsStore(VariablesStack& self, const Grouped::value_type& group)
{
  auto result = MergeResult::MergedVariable{};
  result.isDirty = false;
  for(auto& var : group.second)
  {
    if(self.shouldStoreIfDirty(var.variable))
    {
      result.storeBlocks.emplace_back(var.variable.pointer, var.variable.value, var.block);
    }
  }
  if(!result.storeBlocks.empty())
  {
    _result.mergedVariables.emplace(group.first, std::move(result));
  }
}

void spirv::VariablesStack::Merger::resolveAsPhi(Variable& myVar, VariablesStack& self,
                                                  const Grouped::value_type& group)
{
  auto result = MergeResult::MergedVariable{};
  // If any block thinks its value is dirty than we have to assume the worst case
  result.isDirty = myVar.isDirty;

  llvm::DenseSet<spv::Block*> writtenBlocks;

  if(_result.mergeBlock->hasPredecessor(self.top().block))
  {
    result.phiBlocks.emplace_back(myVar.value, self.top().block);
  }
  for(auto& var : group.second)
  {
    result.phiBlocks.emplace_back(var.variable.value, var.block);
    result.isDirty = result.isDirty || var.variable.isDirty;
    writtenBlocks.insert(var.block);
  }
  for(auto b : _blocks)
  {
    if(writtenBlocks.find(b) == writtenBlocks.end())
    {
      result.phiBlocks.emplace_back(myVar.value, b);
    }
  }
  _result.mergedVariables.emplace(group.first, std::move(result));
}

template<class Iter>
void spirv::VariablesStack::Merger::groupByVarDecl(Iter first, Iter last)
{
  // Separate all variables and process them in batches
  for(; first != last; ++first)
  {
    // If this block doesn't directly branch to the merge block it's a break or
    // continue block in a loop/selection merge block and deoesn't contribute
    // to the merge of an if/then/else or switch.
    if(_result.mergeBlock->hasPredecessor(first->block))
    {
      _blocks.push_back(first->block);
      for(auto& var : first->vars)
      {
        auto decl = var.decl;
        _grouped[decl].push_back({var, first->block});
      }
    }
  }
  std::sort(_blocks.begin(), _blocks.end());
  _blocks.erase(std::unique(_blocks.begin(), _blocks.end()), _blocks.end());
}

////////////////////////////////////////////////////////////////////////////////
// VariablesStack
//

spirv::VariablesStack::VariablesStack(TypeCache& types, spv::Builder& builder, spv::Block* block)
: _types(types)
, _builder(builder)
{
  push(block, nullptr, nullptr);
}

Id spirv::VariablesStack::initUndefined(const VarDecl* decl)
{
  _initing = nullptr;
  auto id = _builder.createUndefined(_types[decl->getType()]);
  trackVariable(*decl, id);
  return id;
}

Id spirv::VariablesStack::load(const VarDecl& decl)
{
  if(_initing == &decl)
  {
    // A local variable is accessed in its own initializer.
    // Produce an undefined value and save it for later.
    return initUndefined(&decl);
  }

  auto& var = find(&decl);

  if(var.value == spv::NoResult)
  {
    assert(var.pointer != spv::NoResult && "not a pointer?");
    auto id = _builder.createLoad(var.pointer);
    var.isDirty = false;
    if(!var.isVolatile)
    {
      var.value = id;
    }
  }
  else if(top().loop)
  {
    top().loop->addRewriteCandidate(&decl, var.value, top().block);
  }
  return var.value;
}

Id spirv::VariablesStack::load(const ExprResult& expr)
{
  if(expr.variable)
  {
    // TODO: access chain and composite extract
    return load(*expr.variable);
  }
  else
  {
    return expr.value;
  }
}

spirv::ExprResult spirv::VariablesStack::store(const ExprResult& target, Id value)
{
  assert(value != spv::NoResult && "must have a real value");
  // TODO: composite access chains
  if(target.variable)
  {
    auto copy = [this, value]
    {
      // If the RHS is a constant we have to create a copy to avoid
      // self-referencing in operations with constant operands of the same value.
      // This also gives every local variable a unique <id> to start with.
      // Also avoids aliasing of two variables for the same reason.
      // TODO: references
      return isConstOrSpec(_builder, value) ? _builder.createCopyObject(value) : value;
    };

    if(_initing == target.variable)
    {
      // A local variable is being initialized with its first value.
      _initing = nullptr;
      value = copy();
      trackVariable(*target.variable, value);
      return {value, target.variable, {}};
    }

    if(target.value == value)
      return target; // This is a no-op

    auto& var = find(target.variable);
    if(var.isVolatile)
    {
      _builder.createStore(value, var.pointer);
      return {spv::NoResult, target.variable, {}};
    }
    else
    {
      var.value = copy();
      var.isDirty = true;
      return {var.value, target.variable, {}};
    }
  }
  llvm_unreachable("assigning to temporary");
//  else
//  {
//    // Somehow we managed to assign to a temporary?
//    return {value, nullptr, {}};
//  }
}

spirv::ExprResult spirv::VariablesStack::store(const ExprResult& target,
                                               const ExprResult& source)
{
  if(!target.variable)
    llvm_unreachable("assigning to temporary");

  // If we are assigning from a variable we have to create a copy to avoid aliasing.
  // TODO: references
  if(source.variable)
  {
    if(source.variable != target.variable)
    {
      if(_initing == target.variable)
      {
        return store(target, _builder.createCopyObject(load(source)));
      }
      else
      {
        auto&var = find(target.variable);
        if(!var.isVolatile)
        {
          return store(target, _builder.createCopyObject(load(source)));
        }
      }
    }
    else
    {
      return target;
    }
  }
  return store(target, source.value);
}

bool spirv::VariablesStack::shouldStoreIfDirty(const Variable& var)
{
  if(var.pointer == spv::NoResult)
  {
    return false;
  }
  else if(var.isVolatile)
  {
    return false; // Never dirty
  }
  else if(var.isDirty)
  {
    return true;
  }
  return false;
}

void spirv::VariablesStack::markAsInitializing(const VarDecl& decl)
{
  assert(!_initing && "cannot initialize two variables at once");
  _initing = &decl;
}

void spirv::VariablesStack::trackVariable(const VarDecl& decl, Id id)
{
  auto* ref = decl.getType()->getAs<ReferenceType>();
  auto isReference = ref != nullptr;
  auto storage = spv::StorageClassFunction; // TODO: derive storage class from variable attributes
  top().vars.push_back({
    &decl,
    isReference ? id : spv::NoResult,
    isReference ? spv::NoResult : id,
    storage,
    isReference && ref->getPointeeType().isVolatileQualified(),
    false,
  });
  sort(top());
}

void spirv::VariablesStack::setTopBlock(spv::Block* block)
{
  top().block = block;
}

spirv::BlockVariables spirv::VariablesStack::extractTop()
{
  auto temp = std::move(top());
  top().block = nullptr;
  top().loop = nullptr;
  return temp;
}

spirv::BlockVariables spirv::VariablesStack::pop()
{
  auto result = std::move(top());
  _stack.pop_back();
  return result;
}

void spirv::VariablesStack::push(spv::Block* block, LoopContext* loop, SwitchContext* switsh)
{
  _stack.push_back({{}, block, loop, switsh});
}

void spirv::VariablesStack::collapseLoopStackIntoTop()
{
  auto loop = top().loop;
  collapseStackIntoTop([loop] (const BlockVariables& block) { return block.loop == loop; });
}

void spirv::VariablesStack::collapseSwitchStackIntoTop()
{
  auto switsh = top().switsh;
  collapseStackIntoTop([switsh] (const BlockVariables& block) { return block.switsh == switsh; });
}

template<class F>
void spirv::VariablesStack::collapseStackIntoTop(F predicate)
{
  auto variables = std::set<Variable>{};

  for(auto it = _stack.rbegin(), end = _stack.rend();
      it != end && predicate(*it);
      ++it)
  {
    for(auto& var : it->vars)
    {
      variables.insert(var);
    }
  }

  top().vars.assign(variables.begin(), variables.end());
}

spirv::Id spirv::VariablesStack::findPreLoopValue(const VarDecl* decl, LoopContext& loop)
{
  auto it = std::find_if(_stack.rbegin(), _stack.rend(), [&loop] (const BlockVariables& block)
                         {
                           return block.loop != &loop;
                         });
  // The root block is never part of a loop
  assert(it != _stack.rend());
  // Found the pre-header of the current loop, now figure out the value
  // of the requested variable.
  auto* var = tryFindInStack(it, decl);
  return var ? var->value : spv::NoResult;
}

template<class Iter>
spirv::MergeResult spirv::VariablesStack::resolveMerge(Iter first, Iter last, spv::Block* mergeBlock)
{
  return Merger(*this, std::move(first), std::move(last), mergeBlock).result();
}

void spirv::VariablesStack::applyMergeResult(MergeResult& merge)
{
  std::unique_ptr<spv::Instruction> inst;
  for(auto& variable : merge.mergedVariables)
  {
    if(!variable.second.storeBlocks.empty())
    {
      for(const auto& store : variable.second.storeBlocks)
      {
        auto inst = llvm::make_unique<spv::Instruction>(spv::OpStore);
        inst->addIdOperand(store.pointer);
        inst->addIdOperand(store.value);
        store.block->insertInstructionBeforeTerminal(inst.get());
        inst.release();
      }
    }
    else if(!variable.second.phiBlocks.empty())
    {
      auto& var = find(variable.first);
      auto inst = llvm::make_unique<spv::Instruction>(_builder.getUniqueId(), _types[var.decl->getType()], spv::OpPhi);
      for(const auto& phi : variable.second.phiBlocks)
      {
        inst->addIdOperand(phi.value);
        inst->addIdOperand(phi.block->getId());
      }
      merge.mergeBlock->addInstructionAtFront(inst.get());
      var.isDirty = variable.second.isDirty;
      store({spv::NoResult, variable.first, {}}, inst->getResultId());
      variable.second.phiResultId = var.value;
      inst.release();
    }
  }
}

spirv::Variable& spirv::VariablesStack::find(const VarDecl* decl)
{
  auto* var = tryFindInTop(decl);
  if(var)
    return *var;

  var = tryFindInStackBelowTop(decl);
  if(var)
  {
    top().vars.push_back(*var);
    sort(top());
    return *tryFindInTop(decl);
  }
  else
  {
    assert(!decl->hasLocalStorage() && "local var must be tracked before use");
    llvm_unreachable("global/static variables not yet implemented");
  }
}

spirv::Variable* spirv::VariablesStack::tryFindInTop(const VarDecl* decl)
{
  return ::tryFind(decl, top());
}

spirv::Variable* spirv::VariablesStack::tryFindInStackBelowTop(const VarDecl* decl)
{
  // Ignore the top element since we need to distinguish between
  // finding the element in the top or somewhere lower.
  return _stack.size() > 1 ? tryFindInStack(++_stack.rbegin(), decl) : nullptr;
}

spirv::Variable* spirv::VariablesStack::tryFindInStack(Stack::reverse_iterator start, const VarDecl* decl)
{
  spirv::Variable* result = nullptr;
  std::find_if(start, _stack.rend(), [decl, &result] (BlockVariables& blockVars)
               {
                 result = ::tryFind(decl, blockVars);
                 return result != nullptr;
               });
  return result;
}

spirv::Variable* spirv::VariablesStack::tryFind(const VarDecl* decl)
{
  auto* var = tryFindInTop(decl);
  return var ? var : tryFindInStackBelowTop(decl);
}

void spirv::VariablesStack::sort(BlockVariables& blockVars)
{
  std::sort(blockVars.vars.begin(), blockVars.vars.end(),
            [] (const Variable& a, const Variable& b) { return a.decl < b.decl; });
}

////////////////////////////////////////////////////////////////////////////////
// LoopContext
//

template<class F>
spirv::LoopContext::LoopContext(spv::Block* preheader, VariablesStack& vars,
                                          LoopContext* parent, F incDirector)
: _preheader(preheader)
, _parent(parent)
, _vars(vars)
, _incDirector(std::move(incDirector))
{
}

void spirv::LoopContext::addBreakBlock(BlockVariables block)
{
  _breakBlocks.push_back(std::move(block));
}

void spirv::LoopContext::addContinueBlock(BlockVariables block)
{
  _continueBlocks.push_back(std::move(block));
}

void spirv::LoopContext::setHeaderBlock(BlockVariables block)
{
  _headerBlock = std::move(block);
}

void spirv::LoopContext::addRewriteCandidate(const VarDecl* decl, Id operand, spv::Block* block)
{
  VarInfo* entry = tryFind(decl);
  if(entry)
  {
    // We already know about this variable. We are only interested in rewriting
    // the block's instructions if it reads the Result<id> under which the
    // variable is introduced into the loop. Otherwise it reads a transitive
    // value and is therefore not affected.
    if(entry->tentativeId == operand)
    {
      if(!std::binary_search(entry->blocks.begin(), entry->blocks.end(), block))
      {
        entry->blocks.push_back(block);
        std::sort(entry->blocks.begin(), entry->blocks.end());
      }
    }
  }
  else
  {
    // This is a new variable. Remember the block containing the consuming
    // instruction so it can be rewritten if required.
    // If the <id> is not the same as it was before the loop then the variable
    // changed its value without being loaded (e.g. assigned a new value). In
    // that case it is not a candidate for a rewrite.
    auto preLoopValue = _vars.findPreLoopValue(decl, *this);
    if(operand == preLoopValue)
    {
      _rewriteCandidates.push_back({decl, {block}, operand, spv::NoResult});
      sort();
    }
  }
}

void spirv::LoopContext::applyMerge(spv::Block* mergeBlock)
{
  mergeContinueBlocks();
  mergeBreakBlocks(mergeBlock);
  applyRewrites();
}

void spirv::LoopContext::setRewriteId(const VarDecl* decl, Id rewriteId)
{
  VarInfo* entry = tryFind(decl);
  if(entry)
  {
    assert(entry->rewriteId == spv::NoResult && "rewriting same valurable twice");
    entry->rewriteId = rewriteId;
    // When merging the loop header only remember this new value for
    // this variable if the input to it was the same as what was active
    // until after the header block. This ensures that merging the break
    // blocks actually uses this new result if the header only read its value.
    if(auto* mergeVar = ::tryFind(decl, _headerBlock))
    {
      if(mergeVar->value == entry->tentativeId)
      {
        mergeVar->value = rewriteId;
      }
    }
  }
}

void spirv::LoopContext::applyRewrites()
{
  for(auto& rewrite : _rewriteCandidates)
  {
    if(rewrite.rewriteId != spv::NoResult)
    {
      for(auto* block : rewrite.blocks)
      {
        std::for_each(block->begin(), block->end(), [&rewrite, this] (spv::Instruction* inst)
                      {
                        rewriteInstruction(inst, rewrite.tentativeId, rewrite.rewriteId);
                      });
      }
    }
  }
}

void spirv::LoopContext::rewriteInstruction(spv::Instruction* inst, Id oldId, Id rewriteId)
{
  auto rewrite = [inst, oldId, rewriteId] (std::initializer_list<unsigned int> indices)
  {
    for(auto i : indices)
    {
      inst->rewriteOperand(oldId, rewriteId, i);
    }
  };
  auto rewriteAll = [inst, oldId, rewriteId]
  {
    inst->rewriteOperands(oldId, rewriteId);
  };
  // We are only interested in rewriting operations with <id> of other values.
  // List all enumerants explicitly so we get a warning if new ones appear.
  switch(inst->getOpCode())
  {
    // These opcodes have no operand <id>s or are not used inside functions
    case spv::OpNop:
    case spv::OpUndef:
    case spv::OpSource:
    case spv::OpSourceExtension:
    case spv::OpName:
    case spv::OpMemberName:
    case spv::OpString:
    case spv::OpLine:
    case spv::OpDecorate:
    case spv::OpMemberDecorate:
    case spv::OpDecorationGroup:
    case spv::OpGroupDecorate:
    case spv::OpGroupMemberDecorate:
    case spv::OpExtension:
    case spv::OpExtInstImport:
    case spv::OpExtInst:
    case spv::OpMemoryModel:
    case spv::OpEntryPoint:
    case spv::OpExecutionMode:
    case spv::OpCapability:
    case spv::OpTypeVoid:
    case spv::OpTypeBool:
    case spv::OpTypeInt:
    case spv::OpTypeFloat:
    case spv::OpTypeVector:
    case spv::OpTypeMatrix:
    case spv::OpTypeImage:
    case spv::OpTypeSampler:
    case spv::OpTypeSampledImage:
    case spv::OpTypeArray:
    case spv::OpTypeRuntimeArray:
    case spv::OpTypeStruct:
    case spv::OpTypeOpaque:
    case spv::OpTypePointer:
    case spv::OpTypeFunction:
    case spv::OpTypeEvent:
    case spv::OpTypeDeviceEvent:
    case spv::OpTypeReserveId:
    case spv::OpTypeQueue:
    case spv::OpTypePipe:
    case spv::OpConstantTrue:
    case spv::OpConstantFalse:
    case spv::OpConstant:
    case spv::OpConstantComposite:
    case spv::OpConstantSampler:
    case spv::OpConstantNull:
    case spv::OpSpecConstantTrue:
    case spv::OpSpecConstantFalse:
    case spv::OpSpecConstant:
    case spv::OpSpecConstantComposite:
    case spv::OpSpecConstantOp:
    case spv::OpVariable:
    case spv::OpFunction:
    case spv::OpFunctionParameter:
    case spv::OpFunctionEnd:
      return;

    // These are used in functions but their operands aren't value <id>s
    // or constants/literals only
    case spv::OpSelectionMerge:
    case spv::OpLoopMerge:
    case spv::OpLabel:
    case spv::OpBranch:
    case spv::OpKill:
    case spv::OpReturn:
    case spv::OpUnreachable:
    case spv::OpEmitVertex:
    case spv::OpEndPrimitive:
    case spv::OpEmitStreamVertex:
    case spv::OpEndStreamPrimitive:
    case spv::OpControlBarrier:
    case spv::OpMemoryBarrier:
      return;

    // OpPhi is a special case because we must not rewrite the value that is
    // inserted from the loop's pre-header block.
    case spv::OpPhi:
      assert((inst->getNumOperands() % 2) == 0 && "OpPhi must have even operand count");
      for(auto i = 0; i < inst->getNumOperands(); i += 2)
      {
        if(inst->getIdOperand(i + 1) != _preheader->getId())
        {
          inst->rewriteOperand(oldId, rewriteId, i);
        }
      }
      return;

    // These are single-operand instructions where the <id> may be followed by
    // non-<id> values.
    case spv::OpLoad:
    case spv::OpAccessChain:
    case spv::OpInBoundsAccessChain:
    case spv::OpArrayLength:
    case spv::OpCompositeExtract:
    case spv::OpBranchConditional:
    case spv::OpSwitch:
    case spv::OpLifetimeStart:
    case spv::OpLifetimeStop:
    case spv::OpAtomicLoad:
    case spv::OpAtomicStore:
    case spv::OpAtomicIIncrement:
    case spv::OpAtomicIDecrement:
      rewrite({0});
      return;

    // These are dual-operand instructions where the <id>s may be followed by
    // non-<id> values.
    case spv::OpStore:
    case spv::OpCopyMemory:
    case spv::OpSampledImage:
    case spv::OpImageSampleImplicitLod:
    case spv::OpImageSampleExplicitLod:
    case spv::OpImageSampleProjImplicitLod:
    case spv::OpImageSampleProjExplicitLod:
    case spv::OpImageFetch:
    case spv::OpVectorShuffle:
    case spv::OpCompositeInsert:
      rewrite({0, 1});
      return;

    // These are triple-operand instructions where the <id>s may be followed by
    // non-<id> values.
    case spv::OpImageTexelPointer:
    case spv::OpImageSampleDrefImplicitLod:
    case spv::OpImageSampleDrefExplicitLod:
    case spv::OpImageSampleProjDrefImplicitLod:
    case spv::OpImageSampleProjDrefExplicitLod:
    case spv::OpImageGather:
    case spv::OpImageDrefGather:
      rewrite({0, 1, 2});
      return;

    // Atomic instructions have their own format
    case spv::OpAtomicExchange:
    case spv::OpAtomicIAdd:
    case spv::OpAtomicISub:
    case spv::OpAtomicSMin:
    case spv::OpAtomicUMin:
    case spv::OpAtomicSMax:
    case spv::OpAtomicUMax:
    case spv::OpAtomicAnd:
    case spv::OpAtomicOr:
    case spv::OpAtomicXor:
      rewrite({0, 3});
      return;

    case spv::OpAtomicCompareExchange:
      rewrite({0, 4, 5});
      return;

    // Thse have only <id> operands
    case spv::OpFunctionCall:
    case spv::OpImageRead:
    case spv::OpImageWrite:
    case spv::OpImageQuerySizeLod:
    case spv::OpImageQuerySize:
    case spv::OpImageQueryLod:
    case spv::OpImageQueryLevels:
    case spv::OpImageQuerySamples:
    case spv::OpConvertFToU:
    case spv::OpConvertFToS:
    case spv::OpConvertSToF:
    case spv::OpConvertUToF:
    case spv::OpUConvert:
    case spv::OpSConvert:
    case spv::OpFConvert:
    case spv::OpQuantizeToF16:
    case spv::OpBitcast:
    case spv::OpVectorExtractDynamic:
    case spv::OpVectorInsertDynamic:
    case spv::OpCompositeConstruct:
    case spv::OpCopyObject:
    case spv::OpTranspose:
    case spv::OpSNegate:
    case spv::OpFNegate:
    case spv::OpIAdd:
    case spv::OpFAdd:
    case spv::OpISub:
    case spv::OpFSub:
    case spv::OpIMul:
    case spv::OpFMul:
    case spv::OpUDiv:
    case spv::OpSDiv:
    case spv::OpFDiv:
    case spv::OpUMod:
    case spv::OpSRem:
    case spv::OpSMod:
    case spv::OpFRem:
    case spv::OpFMod:
    case spv::OpVectorTimesScalar:
    case spv::OpMatrixTimesScalar:
    case spv::OpVectorTimesMatrix:
    case spv::OpMatrixTimesVector:
    case spv::OpMatrixTimesMatrix:
    case spv::OpOuterProduct:
    case spv::OpDot:
    case spv::OpShiftRightLogical:
    case spv::OpShiftRightArithmetic:
    case spv::OpShiftLeftLogical:
    case spv::OpBitwiseOr:
    case spv::OpBitwiseXor:
    case spv::OpBitwiseAnd:
    case spv::OpNot:
    case spv::OpBitFieldInsert:
    case spv::OpBitFieldSExtract:
    case spv::OpBitFieldUExtract:
    case spv::OpBitReverse:
    case spv::OpBitCount:
    case spv::OpAny:
    case spv::OpAll:
    case spv::OpIsNan:
    case spv::OpIsInf:
    case spv::OpLogicalEqual:
    case spv::OpLogicalNotEqual:
    case spv::OpLogicalOr:
    case spv::OpLogicalAnd:
    case spv::OpLogicalNot:
    case spv::OpSelect:
    case spv::OpIEqual:
    case spv::OpINotEqual:
    case spv::OpUGreaterThan:
    case spv::OpSGreaterThan:
    case spv::OpUGreaterThanEqual:
    case spv::OpSGreaterThanEqual:
    case spv::OpULessThan:
    case spv::OpSLessThan:
    case spv::OpULessThanEqual:
    case spv::OpSLessThanEqual:
    case spv::OpFOrdEqual:
    case spv::OpFUnordEqual:
    case spv::OpFOrdNotEqual:
    case spv::OpFUnordNotEqual:
    case spv::OpFOrdLessThan:
    case spv::OpFUnordLessThan:
    case spv::OpFOrdGreaterThan:
    case spv::OpFUnordGreaterThan:
    case spv::OpFOrdLessThanEqual:
    case spv::OpFUnordLessThanEqual:
    case spv::OpFOrdGreaterThanEqual:
    case spv::OpFUnordGreaterThanEqual:
    case spv::OpDPdx:
    case spv::OpDPdy:
    case spv::OpFwidth:
    case spv::OpDPdxFine:
    case spv::OpDPdyFine:
    case spv::OpFwidthFine:
    case spv::OpDPdxCoarse:
    case spv::OpDPdyCoarse:
    case spv::OpFwidthCoarse:
    case spv::OpReturnValue:
      rewriteAll();
      return;

    case spv::OpConvertPtrToU:
    case spv::OpConvertUToPtr:
    case spv::OpCopyMemorySized:
    case spv::OpPtrAccessChain:
      llvm_unreachable("Address capability ops not supported");

    case spv::OpGenericPtrMemSemantics:
    case spv::OpImageQueryDim:
    case spv::OpImageQueryFormat:
    case spv::OpImageQueryOrder:
    case spv::OpSatConvertSToU:
    case spv::OpSatConvertUToS:
    case spv::OpPtrCastToGeneric:
    case spv::OpGenericCastToPtr:
    case spv::OpGenericCastToPtrExplicit:
    case spv::OpIsFinite:
    case spv::OpIsNormal:
    case spv::OpSignBitSet:
    case spv::OpLessOrGreater:
    case spv::OpOrdered:
    case spv::OpUnordered:
    case spv::OpAtomicCompareExchangeWeak:
    case spv::OpAsyncGroupCopy:
    case spv::OpWaitGroupEvents:
      llvm_unreachable("Kernel capability ops not supported");

    case spv::OpGroupAll:
    case spv::OpGroupAny:
    case spv::OpGroupBroadcast:
    case spv::OpGroupIAdd:
    case spv::OpGroupFAdd:
    case spv::OpGroupFMin:
    case spv::OpGroupUMin:
    case spv::OpGroupSMin:
    case spv::OpGroupFMax:
    case spv::OpGroupUMax:
    case spv::OpGroupSMax:
      llvm_unreachable("Group capability ops not supported");

    case spv::OpEnqueueMarker:
    case spv::OpEnqueueKernel:
    case spv::OpGetKernelNDrangeSubGroupCount:
    case spv::OpGetKernelNDrangeMaxSubGroupSize:
    case spv::OpGetKernelWorkGroupSize:
    case spv::OpGetKernelPreferredWorkGroupSizeMultiple:
    case spv::OpRetainEvent:
    case spv::OpReleaseEvent:
    case spv::OpCreateUserEvent:
    case spv::OpIsValidEvent:
    case spv::OpSetUserEventStatus:
    case spv::OpCaptureEventProfilingInfo:
    case spv::OpGetDefaultQueue:
    case spv::OpBuildNDRange:
      llvm_unreachable("DeviceEnqueue capability ops not supported");

    case spv::OpReadPipe:
    case spv::OpWritePipe:
    case spv::OpReservedReadPipe:
    case spv::OpReservedWritePipe:
    case spv::OpReserveReadPipePackets:
    case spv::OpReserveWritePipePackets:
    case spv::OpCommitReadPipe:
    case spv::OpCommitWritePipe:
    case spv::OpIsValidReserveId:
    case spv::OpGetNumPipePackets:
    case spv::OpGetMaxPipePackets:
    case spv::OpGroupReserveReadPipePackets:
    case spv::OpGroupReserveWritePipePackets:
    case spv::OpGroupCommitReadPipe:
    case spv::OpGroupCommitWritePipe:
      llvm_unreachable("Pipe capability ops not supported");

    case spv::OpIAddCarry:
    case spv::OpISubBorrow:
    case spv::OpIMulExtended:
      llvm_unreachable("TBD");
  }
  llvm_unreachable("unknown opcode");
}

void spirv::LoopContext::mergeContinueBlocks()
{
  auto* headerBlock = _headerBlock.block;
  assert(headerBlock && "header block not set");
  auto merge = _vars.resolveMerge(_continueBlocks.begin(), _continueBlocks.end(), headerBlock);
  _vars.applyMergeResult(merge);
  for(const auto& variable : merge.mergedVariables)
  {
    if(!variable.second.phiBlocks.empty())
    {
      assert(variable.second.phiResultId != spv::NoResult && "OpPhi not generated");
      for(const auto& phi : variable.second.phiBlocks)
      {
        // The OpPhi is itself a rewrite candidate
        addRewriteCandidate(variable.first, phi.value, headerBlock);
        // If this is a nested loop the OpPhi is a rewrite candidate in the parent loop
        if(_parent)
        {
          _parent->addRewriteCandidate(variable.first, phi.value, headerBlock);
        }
      }
      // The rewrite value is the result of the new OpPhi
      setRewriteId(variable.first, variable.second.phiResultId);
    }
  }
}

void spirv::LoopContext::mergeBreakBlocks(spv::Block* mergeBlock)
{
  // The continue blocks have been merged by now so we can now treat the header
  // block like a regular break block.
  _breakBlocks.push_back(std::move(_headerBlock));
  auto merge = _vars.resolveMerge(_breakBlocks.begin(), _breakBlocks.end(), mergeBlock);
  _vars.applyMergeResult(merge);
  // The only thing left to do is add the merge block as a rewrite candidate
  // for any OpPhi generated
  for(const auto& variable : merge.mergedVariables)
  {
    if(!variable.second.phiBlocks.empty())
    {
      for(const auto& phi : variable.second.phiBlocks)
      {
        // The OpPhi is itself a rewrite candidate
        addRewriteCandidate(variable.first, phi.value, mergeBlock);
      }
    }
  }
}

bool spirv::LoopContext::invokeIncDirector(FunctionBuilder& builder)
{
  return !_incDirector || _incDirector(builder);
}

spirv::LoopContext::VarInfo* spirv::LoopContext::tryFind(const VarDecl* decl)
{
  struct Comp
  {
    bool operator()(const VarDecl* decl, const VarInfo& info) const { return info.var < decl; }
    bool operator()(const VarInfo& info, const VarDecl* decl) const { return info.var < decl; }
  };
  auto it = std::lower_bound(_rewriteCandidates.begin(), _rewriteCandidates.end(), decl, Comp{});
  return it == _rewriteCandidates.end() ? nullptr : (it->var == decl ? &*it : nullptr);
}

void spirv::LoopContext::sort()
{
  std::sort(_rewriteCandidates.begin(), _rewriteCandidates.end(), [] (const VarInfo& a, const VarInfo& b)
            {
              return a.var < b.var;
            });
}

////////////////////////////////////////////////////////////////////////////////
// Switchcontext
//

spirv::SwitchContext::SwitchContext(spv::Block* headerBlock, spv::Block* mergeBlock,
                                    spv::Instruction* switchInst)
: _headerBlock(headerBlock)
, _mergeBlock(mergeBlock)
, _switchInst(switchInst)
{
}

void spirv::SwitchContext::addCase(std::uint32_t value, spv::Block* block)
{
  _switchInst->addImmediateOperand(value);
  _switchInst->addIdOperand(block->getId());
}

void spirv::SwitchContext::setDefault(spv::Block* block)
{
  assert(_defaultBlock == spv::NoResult && "multiple default cases?");
  _defaultBlock = block->getId();
  _switchInst->rewriteOperand(spv::NoResult, _defaultBlock, 1);
}

void spirv::SwitchContext::addBreakBlock(BlockVariables block)
{
  _breakBlocks.push_back(std::move(block));
}

void spirv::SwitchContext::mergeBreakBlocks(VariablesStack& vars)
{
  vars.applyMergeResult(vars.resolveMerge(_breakBlocks.begin(), _breakBlocks.end(), _mergeBlock));
}

void spirv::SwitchContext::rememberPendingMerge(BlockVariables predecessor)
{
  assert(!_pendingMerge && "two merges pending at once!");
  _pendingMerge = std::move(predecessor);
}

void spirv::SwitchContext::applyPendingMerge(VariablesStack& vars, spv::Block* targetBlock)
{
  if(_pendingMerge)
  {
    vars.setTopBlock(_headerBlock);
    vars.applyMergeResult(vars.resolveMerge(&*_pendingMerge, &*_pendingMerge + 1, targetBlock));
    vars.setTopBlock(targetBlock);
    _pendingMerge.reset();
  }
}

////////////////////////////////////////////////////////////////////////////////
// BreakStack
//

class spirv::BreakStack::PushLoopScoped
{
public:
  PushLoopScoped(BreakStack& stack) : _stack(stack) { _stack.push(loopBreak); }
  ~PushLoopScoped() { _stack.pop(); }

private:
  BreakStack& _stack;
};

class spirv::BreakStack::PushSwitchScoped
{
public:
  PushSwitchScoped(BreakStack& stack) : _stack(stack) { _stack.push(switchBreak); }
  ~PushSwitchScoped() { _stack.pop(); }

private:
  BreakStack& _stack;
};

////////////////////////////////////////////////////////////////////////////////
// StmtBuilder
//

spirv::StmtBuilder::StmtBuilder(TypeCache& types, VariablesStack& variables)
: _types(types)
, _builder(types.builder())
, _vars(variables)
{
}

template<class RHS, class LHS>
bool spirv::StmtBuilder::emitBinaryOperator(const BinaryOperator& expr, RHS lhs, LHS rhs)
{
  if(lhs(*this))
  {
    auto op1 = _subexpr;
    if(rhs(*this))
    {
      auto type = expr.getType();
      // The children of a BinaryOperator always have the same types
      // so it's enough to only check one.
      auto floating = expr.getLHS()->getType()->isFloatingType();
      auto sign = expr.getLHS()->getType()->isSignedIntegerOrEnumerationType();
      switch(expr.getOpcode())
      {
        case BO_MulAssign:
        case BO_DivAssign:
        case BO_RemAssign:
        case BO_AddAssign:
        case BO_SubAssign:
        case BO_ShlAssign:
        case BO_ShrAssign:
        case BO_AndAssign:
        case BO_XorAssign:
        case BO_OrAssign:
          _subexpr = store(op1, _builder.createBinOp(opcode(expr.getOpcode(), floating, sign),
                                                     _types[type],
                                                     load(op1),
                                                     load(_subexpr)));
          break;
        case BO_Assign:
          _subexpr = store(op1, _subexpr);
          break;
        case BO_Comma:
          // Just drop the first operand and return the second
          // Leave _subexpr as-is
          break;
        default: {
          auto id = _builder.createBinOp(opcode(expr.getOpcode(), floating, sign),
                                         _types[type],
                                         load(op1),
                                         load(_subexpr));
          _subexpr = {id, nullptr, {}};
          break;
        }
      }
    }
  }
  return true;
}

bool spirv::StmtBuilder::emitBooleanLiteral(const CXXBoolLiteralExpr& expr)
{
  _subexpr.reset();
  _subexpr.value = _builder.makeBoolConstant(expr.getValue());
  return true;
}

bool spirv::StmtBuilder::emitFloatingLiteral(const FloatingLiteral& expr)
{
  _subexpr.reset();
  auto value = expr.getValue();
  switch(expr.getType()->getAs<BuiltinType>()->getKind())
  {
    case BuiltinType::Half:
      // TODO: half float
      llvm_unreachable("half float not yet implemented");
      break;
    case BuiltinType::Float:
      _subexpr.value = _builder.makeFloatConstant(value.convertToFloat());
      break;
    case BuiltinType::Double:
    case BuiltinType::LongDouble:
      _subexpr.value = _builder.makeDoubleConstant(value.convertToDouble());
      break;
    default:
      llvm_unreachable("invalid type for floating literal");
  }
  return true;
}

template<class F>
bool spirv::StmtBuilder::emitCast(const CastExpr& expr, F subexpr)
{
  switch(expr.getCastKind())
  {
    case clang::CK_LValueToRValue:
      return subexpr(*this); // Nothing to do here

    default:
      llvm_unreachable("cast not implemented");
  }
}

bool spirv::StmtBuilder::emitIntegerLiteral(const IntegerLiteral& expr)
{
  // Literals are never negative
  // TODO: if the literal is directly preceded by an unary minus we should fold
  // it together to distinguish signed/unsigned integer constants.
  auto value = expr.getValue().getZExtValue();
  auto type = expr.getType();
  _subexpr.reset();
  switch(type->getAs<BuiltinType>()->getKind())
  {
    case BuiltinType::Int:
      _subexpr.value = _builder.makeIntConstant(static_cast<int>(value));
      break;
    case BuiltinType::UInt:
      _subexpr.value = _builder.makeUintConstant(static_cast<unsigned int>(value));
      break;
    case BuiltinType::Long:
    case BuiltinType::LongLong:
      // TODO: int64
      llvm_unreachable("int64 not yet implemented");
    case BuiltinType::ULong:
    case BuiltinType::ULongLong:
      // TODO: uint64
      llvm_unreachable("uint64 not yet implemented");
    default:
      llvm_unreachable("invalid type for integer literal");
  }
  return true;
}

template<class F>
bool spirv::StmtBuilder::emitParenExpr(F subexpr)
{
  // Don't care about parentheses.
  // What matters is the order of evaluation.
  return subexpr(*this);
}

template<class F>
bool spirv::StmtBuilder::emitUnaryOperator(const UnaryOperator& expr, F subexpr)
{
  if(subexpr(*this))
  {
    auto type = expr.getType();
    auto floating = type->isFloatingType();
    auto sign = type->isSignedIntegerOrEnumerationType();
    switch(expr.getOpcode())
    {
      case UO_Minus:
      case UO_Not:
      case UO_LNot:
        _subexpr = makeRValue(_builder.createUnaryOp(opcode(expr.getOpcode(), floating, sign),
                                                     _types[type],
                                                     load(_subexpr)));
        break;
      case UO_Plus:
        // Nothing to do, leve _subexpr unchanged
        break;

      case UO_PreInc:
        _subexpr = makePrefixOp(_subexpr, type, IncDecOperator::inc);
        break;
      case UO_PreDec:
        _subexpr = makePrefixOp(_subexpr, type, IncDecOperator::dec);
        break;
      case UO_PostInc:
        _subexpr = makePostfixOp(_subexpr, type, IncDecOperator::inc);
        break;
      case UO_PostDec:
        _subexpr = makePostfixOp(_subexpr, type, IncDecOperator::dec);
        break;

        llvm_unreachable("inc/dec should have been special cased");
      case UO_AddrOf:
      case UO_Deref:
      case UO_Real:
      case UO_Imag:
      case UO_Extension:
        llvm_unreachable("operator not supported");
    }
  }
  return true;
}

bool spirv::StmtBuilder::emitVariableAccess(const VarDecl& var)
{
  // TODO: combine with access chain
  _subexpr = {spv::NoResult, &var, _subexpr.chain};
  return true;
}

spirv::ExprResult spirv::StmtBuilder::makePrefixOp(const ExprResult& lvalue, QualType type, IncDecOperator op)
{
  IncDecLiteral literal = makeLiteralForIncDec(type);
  return store(lvalue, _builder.createBinOp(opcode(op == IncDecOperator::inc ? BO_Add : BO_Sub,
                                                   literal.floating,
                                                   literal.sign),
                                            _types[type],
                                            load(lvalue),
                                            literal.id));
}

spirv::ExprResult spirv::StmtBuilder::makePostfixOp(const ExprResult& lvalue, QualType type, IncDecOperator op)
{
  auto original = makeRValue(load(lvalue));

  IncDecLiteral literal = makeLiteralForIncDec(type);
  store(lvalue, _builder.createBinOp(opcode(op == IncDecOperator::inc ? BO_Add : BO_Sub,
                                            literal.floating,
                                            literal.sign),
                                     _types[type],
                                     load(lvalue),
                                     literal.id));
  return original;
}

spirv::StmtBuilder::IncDecLiteral spirv::StmtBuilder::makeLiteralForIncDec(QualType type)
{
  switch(type->getAs<BuiltinType>()->getKind())
  {
    case BuiltinType::Short:
      // TODO: int16
      llvm_unreachable("int16 not yet implemented");
    case BuiltinType::UShort:
      // TODO: uint16
      llvm_unreachable("uint16 not yet implemented");
    case BuiltinType::Int:
      return {_builder.makeIntConstant(1), false, true};
    case BuiltinType::UInt:
      return{_builder.makeUintConstant(1), false, false};
    case BuiltinType::Long:
    case BuiltinType::LongLong:
      // TODO: int64
      llvm_unreachable("int64 not yet implemented");
    case BuiltinType::ULong:
    case BuiltinType::ULongLong:
      // TODO: uint64
      llvm_unreachable("uint64 not yet implemented");
    case BuiltinType::Half:
      // TODO: float16
      llvm_unreachable("float16 not yet implemented");
    case BuiltinType::Float:
      return {_builder.makeFloatConstant(1), true, false};
    case BuiltinType::Double:
    case BuiltinType::LongDouble:
      return {_builder.makeDoubleConstant(1), true, false};
    default:
      llvm_unreachable("invalid type for unary operator");
  }
}


////////////////////////////////////////////////////////////////////////////////
// FunctionBuilder
//

spirv::FunctionBuilder::FunctionBuilder(FunctionDecl& decl, TypeCache& types, TypeMangler& mangler)
: _types(types)
, _mangler(mangler)
, _decl(decl)
{
}

bool spirv::FunctionBuilder::addParam(const ParmVarDecl& param)
{
  _params.push_back(&param);
  return true;
}

template<class F>
bool spirv::FunctionBuilder::buildBreakStmt(F&& cleanupDirector)
{
  applyPendingSwitchMerge();
  if(cleanupDirector(*this))
  {
    if(_breaks.isLoop())
    {
      _vars.collapseLoopStackIntoTop();
      _loops.top()->addBreakBlock(_vars.extractTop());
      _builder.createLoopExit();
      return true;
    }
    else
    {
      auto* switsh = _switches.top();
      _vars.collapseSwitchStackIntoTop();
      switsh->addBreakBlock(_vars.extractTop());

      auto branch = llvm::make_unique<spv::Instruction>(spv::OpBranch);
      branch->addIdOperand(switsh->mergeBlock()->getId());
      _builder.getBuildPoint()->addInstruction(branch.get());
      _builder.getBuildPoint()->addSuccessor(switsh->mergeBlock());
      switsh->mergeBlock()->addPredecessor(_builder.getBuildPoint());
      branch.release();

      // Add unreachable past-break block
      auto block = llvm::make_unique<spv::Block>(_builder.getUniqueId(), _builder.getBuildPoint()->getParent());
      block->setUnreachable();
      _builder.getBuildPoint()->getParent().addBlock(block.get());
      _builder.setBuildPoint(block.release());

      return true;
    }
  }
  return false;
}

template<class F>
bool spirv::FunctionBuilder::buildContinueStmt(F&& cleanupDirector)
{
  applyPendingSwitchMerge();
  if(cleanupDirector(*this))
  {
    _loops.top()->invokeIncDirector(*this);
    _vars.collapseLoopStackIntoTop();
    _loops.top()->addContinueBlock(_vars.extractTop());
    _builder.createLoopContinue();
    return true;
  }
  return false;
}

template<class F1, class F2>
bool spirv::FunctionBuilder::buildDoStmt(F1 condDirector, F2 bodyDirector)
{
  applyPendingSwitchMerge();
  return buildSimpleLoopCommon(false, std::move(condDirector), std::move(bodyDirector));
}

template<class F1, class F2, class F3, class F4>
bool spirv::FunctionBuilder::buildForStmt(bool hasCond,
                                          F1 initDirector, F2 condDirector,
                                          F3 incDirector, F4 bodyDirector)
{
  applyPendingSwitchMerge();
  if(initDirector(*this))
  {
    _vars.setTopBlock(_builder.getBuildPoint());
    LoopContext loop{_builder.getBuildPoint(), _vars, _loops.top(), [incDirector] (FunctionBuilder& builder)
    {
      StmtBuilder incStmt{builder._types, builder._vars};
      return incDirector(incStmt);
    }};
    LoopStack::PushScoped pushLoop{_loops, &loop};
    BreakStack::PushLoopScoped pushBreak{_breaks};

    _builder.makeNewLoop(true);
    auto* testBlock = _builder.getBuildPoint();
    pushBlockVars(testBlock);
    if(hasCond)
    {
      StmtBuilder condStmt{_types, _vars};
      if(!condDirector(condStmt))
        return false;
      _builder.createLoopTestBranch(load(condStmt.expr()));
    }
    else
    {
      _builder.createBranchToBody();
    }
    auto* bodyBlock = _builder.getBuildPoint();
    pushBlockVars(bodyBlock);
    if(bodyDirector(*this))
    {
      StmtBuilder incStmt{_types, _vars};
      if(incDirector(incStmt))
      {
        _builder.closeLoop();
        auto mergeBlock = _builder.getBuildPoint();
        _vars.collapseLoopStackIntoTop();
        loop.addContinueBlock(popBlockVars());
        loop.setHeaderBlock(popBlockVars());
        loop.applyMerge(mergeBlock);
        _vars.setTopBlock(mergeBlock);
        return true;
      }
    }
  }
  return false;
}

template<class F1, class F2>
bool spirv::FunctionBuilder::buildIfStmt(F1 condDirector, F2 thenDirector)
{
  applyPendingSwitchMerge();
  StmtBuilder condStmt{_types, _vars};
  if(condDirector(condStmt))
  {
    _vars.setTopBlock(_builder.getBuildPoint());

    spv::Builder::If ifBuilder{load(condStmt.expr()), _builder};
    pushBlockVars();

    if(thenDirector(*this))
    {
      ifBuilder.makeEndIf();
      auto thenVars = popBlockVars();

      _vars.applyMergeResult(_vars.resolveMerge(&thenVars, &thenVars + 1, _builder.getBuildPoint()));
      _vars.setTopBlock(_builder.getBuildPoint());

      return true;
    }
  }
  return false;
}

template<class F1, class F2, class F3>
bool spirv::FunctionBuilder::buildIfStmt(F1 condDirector, F2 thenDirector, F3 elseDirector)
{
  applyPendingSwitchMerge();
  StmtBuilder condStmt{_types, _vars};
  if(condDirector(condStmt))
  {
    _vars.setTopBlock(_builder.getBuildPoint());

    spv::Builder::If ifBuilder{load(condStmt.expr()), _builder};

    llvm::SmallVector<BlockVariables, 2> blockVars;

    pushBlockVars();
    if(thenDirector(*this))
    {
      blockVars.push_back(popBlockVars());
      ifBuilder.makeBeginElse();

      pushBlockVars();
      if(elseDirector(*this))
      {
        ifBuilder.makeEndIf();
        blockVars.push_back(popBlockVars());

        _vars.applyMergeResult(_vars.resolveMerge(blockVars.begin(), blockVars.end(), _builder.getBuildPoint()));
        _vars.setTopBlock(_builder.getBuildPoint());
      }
      return true;
    }
  }
  return false;
}

template<class F>
bool spirv::FunctionBuilder::buildReturnStmt(F exprDirector)
{
  applyPendingSwitchMerge();
  StmtBuilder stmt{_types, _vars};
  if(exprDirector(stmt))
  {
    bool isVoid = _returnType == _builder.makeVoidType();
    // TODO: flush dirty variables
    _builder.makeReturn(false, // Return stmts from the clang AST are never implicit
                        isVoid ? spv::NoResult : load(stmt.expr()),
                        false);
    return true;
  }
  return false;
}

template<class F>
bool spirv::FunctionBuilder::buildStmt(F exprDirector)
{
  applyPendingSwitchMerge();
  StmtBuilder stmt{_types, _vars};
  return exprDirector(stmt);
}

template<class F1, class F2>
bool spirv::FunctionBuilder::buildSwitchStmt(F1 condDirector, F2 bodyDirector)
{
  applyPendingSwitchMerge();
  StmtBuilder stmt{_types, _vars};
  if(condDirector(stmt))
  {
    auto headerBlock = _builder.getBuildPoint();
    auto mergeBlock = llvm::make_unique<spv::Block>(_builder.getUniqueId(), headerBlock->getParent());

    // We cannot use spv::Builder's switch interface because we don't know all
    // the values and cases up front.
    auto mergeInst = llvm::make_unique<spv::Instruction>(spv::OpSelectionMerge);
    mergeInst->addIdOperand(mergeBlock->getId());
    mergeInst->addImmediateOperand(spv::SelectionControlMaskNone);
    headerBlock->addInstruction(mergeInst.get());
    mergeInst.release();

    auto switchInst = llvm::make_unique<spv::Instruction>(spv::NoResult, spv::NoType, spv::OpSwitch);
    switchInst->addIdOperand(load(stmt.expr()));
    switchInst->addIdOperand(spv::NoResult); // This is later replaced when we know the default situtation
    headerBlock->addInstruction(switchInst.get());

    _vars.setTopBlock(_builder.getBuildPoint());
    SwitchContext switsh{headerBlock, mergeBlock.get(), switchInst.release()};
    SwitchStack::PushScoped pushSwitch{_switches, &switsh};
    BreakStack::PushSwitchScoped pushBreak{_breaks};

    // Precreate the first block. If a new case recognizes that the previous
    // block is empty it is re-used.
    auto block = llvm::make_unique<spv::Block>(_builder.getUniqueId(), headerBlock->getParent());
    block->addPredecessor(headerBlock);
    headerBlock->addSuccessor(block.get());
    headerBlock->getParent().addBlock(block.get());
    _builder.setBuildPoint(block.get());
    block.release();

    pushBlockVars();
    if(bodyDirector(*this))
    {
      // If the final block was not closed add an implicit break
      if(!_builder.getBuildPoint()->isTerminated() && !_builder.getBuildPoint()->isUnreachable())
      {
        auto breakInst = llvm::make_unique<spv::Instruction>(spv::OpBranch);
        breakInst->addIdOperand(mergeBlock->getId());
        _builder.getBuildPoint()->addInstruction(breakInst.get());
        _builder.getBuildPoint()->addSuccessor(mergeBlock.get());
        mergeBlock->addPredecessor(_builder.getBuildPoint());
        breakInst.release();
      }
      switsh.addBreakBlock(popBlockVars());

      headerBlock->getParent().addBlock(mergeBlock.get());
      if(!switsh.hasDefault())
      {
        switsh.setDefault(mergeBlock.get());
        headerBlock->addSuccessor(mergeBlock.get());
        mergeBlock->addPredecessor(headerBlock);
      }
      _builder.setBuildPoint(mergeBlock.get());
      switsh.mergeBreakBlocks(_vars);
      _vars.setTopBlock(mergeBlock.get());
      mergeBlock.release();
      return true;
    }
  }
  return false;
}

bool spirv::FunctionBuilder::buildSwitchCaseStmt(llvm::APSInt value)
{
  auto x = static_cast<std::uint32_t>(value.getZExtValue());
  nextSwitchCaseBlock();
  _switches.top()->addCase(x, _builder.getBuildPoint());
  return true;
}

bool spirv::FunctionBuilder::buildSwitchDefaultStmt()
{
  nextSwitchCaseBlock();
  _switches.top()->setDefault(_builder.getBuildPoint());
  return true;
}

template<class F1, class F2>
bool spirv::FunctionBuilder::buildWhileStmt(F1 condDirector, F2 bodyDirector)
{
  applyPendingSwitchMerge();
  return buildSimpleLoopCommon(true, std::move(condDirector), std::move(bodyDirector));
}

bool spirv::FunctionBuilder::declareUndefinedVar(const VarDecl& var)
{
  applyPendingSwitchMerge();
  StmtBuilder stmt{_types, _vars};
  // Store the variable with an undefined value. We need this to have an operand
  // for OpPhi for all blocks dominated by the current one.
  _builder.addName(_vars.initUndefined(&var), var.getNameAsString().c_str());
  return true;
}

template<class F>
bool spirv::FunctionBuilder::declareVar(const VarDecl& var, F initDirector)
{
  applyPendingSwitchMerge();
  StmtBuilder stmt{_types, _vars};
  // If the variable is loaded before being written it produces an OpUndef value.
  // TODO: references
  _vars.markAsInitializing(var);
  if(initDirector(stmt))
  {
    auto result = store({spv::NoResult, &var, {}}, stmt.expr());
    if(result.value != spv::NoResult)
    {
      _builder.addName(result.value, var.getNameAsString().c_str());
    }
    return true;
  }
  return false;
}

template<class F>
bool spirv::FunctionBuilder::pushScope(F scopeDirector)
{
  if(!_function)
  {
    // This is the first block in the function.
    // This means we know the entire signature and can create the type.
    assert(_returnType != spv::NoType && "return type not yet set");

    auto params = std::vector<Id>{};
    std::transform(_params.begin(), _params.end(),
                   std::back_inserter(params),
                   [this] (const ParmVarDecl* param)
    {
      auto type = param->getType();
      // Arguments passed by reference are passed as pointer so the original can be modified.
      // This includes const references as even a const member can change "mutable" fields.
      // TODO: If the pointee type has no "mutable" members we could think about dropping the indirection.
      // TODO: It would probably also require banning of const_cast, though...
      // TODO: But if imported functions are a thing they could perform a write anyway...
      if(auto* ref = type->getAs<ReferenceType>())
      {
        return _types.getPointer(ref->getPointeeType(), spv::StorageClassFunction);
      }
      else
      {
        return _types[type];
      }
    });

    std::string name = _mangler.mangleCxxDeclName(_decl);
    spv::Block* block;
    _function = _builder.makeFunctionEntry(_returnType, name.c_str(), params, &block);
    _vars.setTopBlock(block);
    for(auto i = 0u; i < params.size(); ++i)
    {
      _builder.addName(_function->getParamId(i), _params[i]->getNameAsString().c_str());
      _vars.trackVariable(*_params[i], _function->getParamId(i));
    }
  }
  // We don't need any extra setup for a new scope, just continue the existing block.
  // It's an aspect of the frontend we don't have to care about.
  return scopeDirector();
}

bool spirv::FunctionBuilder::setReturnType(QualType type)
{
  assert(_returnType == spv::NoType && "return type already set");
  _returnType = _types[type];
  return true;
}

void spirv::FunctionBuilder::applyPendingSwitchMerge()
{
  if(_switches.top())
  {
    _switches.top()->applyPendingMerge(_vars, _builder.getBuildPoint());
  }
}

void spirv::FunctionBuilder::nextSwitchCaseBlock()
{
  auto* prevCaseBlock = _builder.getBuildPoint();
  // If the current block has only a single instruction then it is OpLabel
  // and means the previous case label contained no statements, in which
  // case we simply re-use the same block as jump target.
  if(prevCaseBlock->getNumInstructions() == 1 && !prevCaseBlock->isUnreachable())
    return;

  auto* switsh = _switches.top();
  auto prevCaseVars = popBlockVars();
  auto nextCaseBlock = llvm::make_unique<spv::Block>(_builder.getUniqueId(), prevCaseBlock->getParent());
  nextCaseBlock->addPredecessor(switsh->headerBlock());
  switsh->headerBlock()->addSuccessor(nextCaseBlock.get());
  prevCaseBlock->getParent().addBlock(nextCaseBlock.get());
  _builder.setBuildPoint(nextCaseBlock.get());
  pushBlockVars();

  // If the previous block is not terminated it is a fallthrough to the next case.
  // We have to set a branch and merge variables from the header block and the previous block.
  if(!prevCaseBlock->isTerminated() && !prevCaseBlock->isUnreachable())
  {
    auto branch = llvm::make_unique<spv::Instruction>(spv::OpBranch);
    branch->addIdOperand(nextCaseBlock->getId());
    prevCaseBlock->addInstruction(branch.get());
    prevCaseBlock->addSuccessor(nextCaseBlock.get());
    nextCaseBlock->addPredecessor(prevCaseBlock);
    branch.release();
    switsh->rememberPendingMerge(std::move(prevCaseVars));
  }
  nextCaseBlock.release();
}

template<class F1, class F2>
bool spirv::FunctionBuilder::buildSimpleLoopCommon(bool testFirst, F1 condDirector, F2 bodyDirector)
{
  _vars.setTopBlock(_builder.getBuildPoint());

  StmtBuilder condStmt{_types, _vars};
  LoopContext loop{_builder.getBuildPoint(), _vars, _loops.top(), nullptr};
  LoopStack::PushScoped pushLoop{_loops, &loop};
  BreakStack::PushLoopScoped pushBreak{_breaks};

  _builder.makeNewLoop(testFirst);
  auto* testBlock = _builder.getBuildPoint();
  pushBlockVars(testBlock);
  if(condDirector(condStmt))
  {
    _builder.createLoopTestBranch(load(condStmt.expr()));
    auto* bodyBlock = _builder.getBuildPoint();
    pushBlockVars(bodyBlock);
    if(bodyDirector(*this))
    {
      _builder.closeLoop();
      auto mergeBlock = _builder.getBuildPoint();
      _vars.collapseLoopStackIntoTop();
      loop.addContinueBlock(popBlockVars());
      loop.setHeaderBlock(popBlockVars());
      loop.applyMerge(mergeBlock);
      _vars.setTopBlock(mergeBlock);
      return true;
    }
  }
  return false;
}


Id spirv::FunctionBuilder::finalize()
{
  assert(_function && "there is no function");
  _builder.leaveFunction(false);
  return _function->getId();
}


////////////////////////////////////////////////////////////////////////////////
// ModuleBuilder
//

spirv::ModuleBuilder::ModuleBuilder(ASTContext& ast)
: _ast(ast)
, _mangler(ast)
{
  _builder.setSource(spv::SourceLanguage::SourceLanguageUnknown, 0);
}

std::string spirv::ModuleBuilder::moduleContent()
{
  auto spirv = std::vector<unsigned>{};
  _builder.dump(spirv);
  auto string = std::string{};
  auto n = sizeof(unsigned) * spirv.size();
  string.resize(n);
  std::memcpy(&string[0], spirv.data(), n);
  return string;
}

template<class Director>
bool spirv::ModuleBuilder::buildRecord(QualType type, Director director)
{
  RecordBuilder builder{type, _types, _ast};
  auto success = director(builder);
  if(success)
  {
    _types.add(type->getAsCXXRecordDecl(), builder.finalize());
  }
  return success;
}

template<class Director>
bool spirv::ModuleBuilder::buildFunction(FunctionDecl& decl, Director director)
{
  FunctionBuilder builder{decl, _types, _mangler};
  auto success = director(builder);
  if(success)
  {
    builder.finalize();
  }
  return success;
}
