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
// VariablesStack::Merger
//

class spirv::VariablesStack::Merger
{
public:
  template<class Iter>
  Merger(VariablesStack& self, Iter first, Iter last, bool reachableFromParent);

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
  void storeIfDirty(std::vector<Var>& group);

  Grouped _grouped;
  llvm::SmallVector<spv::Block*, 8> _blocks;
  std::vector<unsigned> _operands;
  bool _reachableFromParent;
};

template<class Iter>
spirv::VariablesStack::Merger::Merger(VariablesStack& self, Iter first, Iter last, bool reachableFromParent)
: _reachableFromParent(reachableFromParent)
{
  groupByVarDecl(std::move(first), std::move(last));
  for(auto& kvp : _grouped)
  {
    processVariable(kvp, self);
  }
}

void spirv::VariablesStack::Merger::processVariable(Grouped::value_type& group,
                                                    VariablesStack& self)
{
  Variable* myVar = group.first->hasLocalStorage() ? &self.find(group.first) : self.tryFindInStack(group.first);
  if(!myVar)
  {
    // The dominating blocks never loaded this global variable.
    // Emit stores in all blocks which loaded it so regardless what block the
    // merge is reached from the next load gets the same value every time.
    storeIfDirty(group.second);
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
      assert(myVar->decl->hasLocalStorage() && myVar->pointer != spv::NoResult && "not a local pointer?");
      storeIfDirty(group.second);
    }
    else
    {
      // Case 3: Some block modified the variable
      // Collect all the participating values. If a block modified the variable
      // inser the new value into OpPhi, for all the others take the value from
      // the dominating block.
      llvm::DenseSet<spv::Block*> writtenBlocks;
      _operands.clear();
      if(_reachableFromParent)
      {
        _operands.push_back(myVar->value);
        _operands.push_back(self.top().block->getId());
      }
      for(auto& var : group.second)
      {
        _operands.push_back(var.variable.value);
        _operands.push_back(var.block->getId());
        writtenBlocks.insert(var.block);
      }
      for(auto b : _blocks)
      {
        if(writtenBlocks.find(b) == writtenBlocks.end())
        {
          _operands.push_back(myVar->value);
          _operands.push_back(self.top().block->getId());
        }
      }
      // TODO: if we're in a loop this becomes a rewrite candidate
      self.store({spv::NoResult, myVar->decl, {}},
                 self._builder.createOp(spv::OpPhi, self._types[myVar->decl->getType()], _operands));
      // If any block thinks its value is dirty than we have to assume the worst case
      myVar->isDirty = myVar->isDirty || std::any_of(group.second.begin(), group.second.end(), [] (const Var& var)
                                                     {
                                                       return var.variable.isDirty;
                                                     });
    }
  }
}

void spirv::VariablesStack::Merger::storeIfDirty(std::vector<Var>& group)
{
  for(auto& var : group)
  {
    VariablesStack::storeIfDirty(var.variable, var.block);
  }
}

template<class Iter>
void spirv::VariablesStack::Merger::groupByVarDecl(Iter first, Iter last)
{
  // Separate all variables and process them in batches
  for(; first != last; ++first)
  {
    _blocks.push_back(first->block);
    for(auto& var : first->vars)
    {
      auto decl = var.decl;
      _grouped[decl].push_back({std::move(var), first->block});
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
  push(block);
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
    if(_initing == target.variable)
    {
      // A local variable is being initialized with its first value.
      _initing = nullptr;
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
      var.value = value;
      var.isDirty = true;
      return {value, target.variable, {}};
    }
  }
  llvm_unreachable("assigning to temporary");
//  else
//  {
//    // Somehow we managed to assign to a temporary?
//    return {value, nullptr, {}};
//  }
}

void spirv::VariablesStack::storeIfDirty(Variable& var)
{
  assert(top().block && "must have a block set");
  return storeIfDirty(var, top().block);
}

void spirv::VariablesStack::storeIfDirty(Variable& var, spv::Block* block)
{
  if(var.pointer == spv::NoResult)
  {
    return;
  }
  else if(var.isVolatile)
  {
    return; // Never dirty
  }
  else if(var.isDirty)
  {
    auto inst = llvm::make_unique<spv::Instruction>(spv::OpStore);
    inst->addIdOperand(var.pointer);
    inst->addIdOperand(var.value);
    block->insertInstructionBeforeTerminal(inst.get());
    inst.release();
    var.isDirty = false;
  }
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

spirv::BlockVariables spirv::VariablesStack::popAndGet()
{
  auto result = std::move(top());
  pop();
  return result;
}

void spirv::VariablesStack::push(spv::Block* block)
{
  _stack.push_back({{}, block});
}

template<class Iter>
void spirv::VariablesStack::merge(Iter first, Iter last, spv::Block* mergeBlock, bool reachableFromParent)
{
  Merger m(*this, std::move(first), std::move(last), reachableFromParent);
}

spirv::Variable& spirv::VariablesStack::find(const VarDecl* decl)
{
  auto* var = tryFind(decl);
  if(var)
    return *var;

  var = tryFindInStack(decl);
  if(var)
  {
    top().vars.push_back(*var);
    sort(top());
    return *tryFind(decl);
  }
  else
  {
    assert(!decl->hasLocalStorage() && "local var must be tracked before use");
    llvm_unreachable("global/static variables not yet implemented");
  }
}

spirv::Variable* spirv::VariablesStack::tryFind(const VarDecl* decl)
{
  return tryFind(decl, top());
}

spirv::Variable* spirv::VariablesStack::tryFind(const VarDecl* decl, BlockVariables& blockVars)
{
  struct Comp
  {
    bool operator()(const VarDecl* decl, const Variable& var) const { return var.decl < decl; }
    bool operator()(const Variable& var, const VarDecl* decl) const { return var.decl < decl; }
  };
  auto it = std::lower_bound(blockVars.vars.begin(), blockVars.vars.end(), decl, Comp{});
  return it == blockVars.vars.end() ? nullptr : (it->decl == decl ? &*it : nullptr);
}

spirv::Variable* spirv::VariablesStack::tryFindInStack(const VarDecl* decl)
{
  // Ignore the top element since we need to distinguish between
  // finding the element in the top or somewhere lower.
  spirv::Variable* result = nullptr;
  if(_stack.size() > 1)
  {
    std::find_if(++_stack.rbegin(), _stack.rend(), [decl, &result] (BlockVariables& blockVars)
                 {
                   result = tryFind(decl, blockVars);
                   return result != nullptr;
                 });
  }
  return result;
}

void spirv::VariablesStack::sort(BlockVariables& blockVars)
{
  std::sort(blockVars.vars.begin(), blockVars.vars.end(),
            [] (const Variable& a, const Variable& b) { return a.decl < b.decl; });
}

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
      auto floating = type->isFloatingType();
      auto sign = type->isSignedIntegerOrEnumerationType();
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
          _subexpr = store(op1, load(_subexpr));
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
    case BuiltinType::Long:
    case BuiltinType::LongLong:
      // TODO: int64
      llvm_unreachable("int64 not yet implemented");
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
      llvm_unreachable("int32 not yet implemented");
    case BuiltinType::UShort:
      // TODO: uint16
      llvm_unreachable("uint32 not yet implemented");
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

template<class F1, class F2>
bool spirv::FunctionBuilder::buildIfStmt(F1 condDirector, F2 thenDirector)
{
  StmtBuilder condStmt{_types, _vars};
  if(condDirector(condStmt))
  {
    _vars.setTopBlock(_builder.getBuildPoint());

    spv::Builder::If ifBuilder{load(condStmt.expr()), _builder};
    _vars.push(_builder.getBuildPoint());

    if(thenDirector(*this))
    {
      ifBuilder.makeEndIf();
      auto thenVars = _vars.popAndGet();

      // TODO: merge
      _vars.merge(&thenVars, &thenVars + 1, _builder.getBuildPoint(), true);
      _vars.setTopBlock(_builder.getBuildPoint());

      return true;
    }
  }
  return false;
}

template<class F1, class F2, class F3>
bool spirv::FunctionBuilder::buildIfStmt(F1 condDirector, F2 thenDirector, F3 elseDirector)
{
  StmtBuilder condStmt{_types, _vars};
  if(condDirector(condStmt))
  {
    _vars.setTopBlock(_builder.getBuildPoint());

    spv::Builder::If ifBuilder{load(condStmt.expr()), _builder};

    llvm::SmallVector<BlockVariables, 2> blockVars;

    _vars.push(_builder.getBuildPoint());
    if(thenDirector(*this))
    {
      blockVars.push_back(_vars.popAndGet());
      ifBuilder.makeBeginElse();

      _vars.push(_builder.getBuildPoint());
      if(elseDirector(*this))
      {
        ifBuilder.makeEndIf();
        blockVars.push_back(_vars.popAndGet());

        // TODO: merge
        _vars.merge(blockVars.begin(), blockVars.end(), _builder.getBuildPoint(), false);
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
  StmtBuilder stmt{_types, _vars};
  return exprDirector(stmt);
}

bool spirv::FunctionBuilder::declareUndefinedVar(const VarDecl& var)
{
  StmtBuilder stmt{_types, _vars};
  // Store the variable with an undefined value. We need this to have an operand
  // for OpPhi for all blocks dominated by the current one.
  _vars.initUndefined(&var);
  return true;
}

template<class F>
bool spirv::FunctionBuilder::declareVar(const VarDecl& var, F initDirector)
{
  StmtBuilder stmt{_types, _vars};
  // If the variable is loaded before being written it produces an OpUndef value.
  // TODO: references
  _vars.markAsInitializing(var);
  if(initDirector(stmt))
  {
    store({spv::NoResult, &var, {}}, load(stmt.expr()));
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
