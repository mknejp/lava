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

spv::Id spirv::TypeCache::get(QualType type) const
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

spv::Id spirv::TypeCache::getPointer(QualType type, spv::StorageClass storage) const
{
  return _builder.makePointer(storage, get(type));
}

void spirv::TypeCache::add(CXXRecordDecl* decl, spv::Id id)
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

spv::Id spirv::RecordBuilder::finalize()
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
// Variables
//

spirv::Variables::Variables(TypeCache& types)
: _types(types)
, _builder(types.builder())
{
}

spv::Id spirv::Variables::load(const VarDecl& decl)
{
  auto& var = find(&decl);
  if(!var.inited)
  {
    // Only local non-pointer variables can be uninitialized
    auto init = llvm::make_unique<spv::Instruction>(_builder.getUniqueId(), _types[decl.getType()], spv::OpUndef);
    _builder.getBuildPoint()->addInstruction(init.get());
    // This is now the official value to avoid more OpUndef instructions in the future
    var.value = init->getResultId();
    var.inited = false;
    init.release();
  }
  else if(var.value == 0)
  {
    auto id = _builder.createLoad(var.pointer);
    var.isDirty = false;
    if(!var.isVolatile)
    {
      var.value = id;
    }
  }
  return var.value;
}

spv::Id spirv::Variables::load(const ExprResult& expr)
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

spirv::ExprResult spirv::Variables::store(const ExprResult& target, spv::Id value)
{
  // TODO: composite access chains
  if(target.variable)
  {
    if(target.value == value)
      return target; // This is a no-op

    auto& var = find(target.variable);
    var.inited = true;
    if(var.isVolatile)
    {
      _builder.createStore(value, var.pointer);
      return {0, target.variable, {}};
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
//    // Somehow we manages to assign to a temporary?
//    return {value, nullptr, {}};
//  }
}

void spirv::Variables::trackVariable(const VarDecl& decl, spv::Id id)
{
  auto* ref = decl.getType()->getAs<ReferenceType>();
  auto isReference = ref != nullptr;
  _vars.push_back({
    &decl,
    isReference ? id : 0,
    isReference ? 0 : id,
    spv::StorageClassFunction,
    isReference && ref->getPointeeType().isVolatileQualified(),
    false,
    id == 0,
  });
}

spirv::Variable& spirv::Variables::find(const VarDecl* decl)
{
  auto it = std::find_if(_vars.begin(), _vars.end(), [decl] (const Variable& v)
                         {
                           return v.var == decl;
                         });
//  assert(it != _vars.end() && "variable not tracked");
  if(it == _vars.end())
  {
    // Figure out what kind of variable this is and start tracking it
    if(decl->isLocalVarDecl())
    {
      if(!decl->isStaticLocal())
      {
        // If a local variable is not tracked yet it means it is not initialized.
        // If loaded it produces an OpUndef value.
        trackVariable(*decl, 0);
        return _vars.back();
      }
      else
        llvm_unreachable("static local variables not yet implemented");
    }
    else
      llvm_unreachable("global variables not yet implemented");
  }
  return *it;
}

////////////////////////////////////////////////////////////////////////////////
// StmtBuilder
//

spirv::StmtBuilder::StmtBuilder(TypeCache& types, Variables& variables)
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
      llvm_unreachable("int64 not yet implemented");
    case BuiltinType::Half:
      // TODO: half float
      llvm_unreachable("half flot not yet implemented");
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
bool spirv::FunctionBuilder::buildStmt(F director)
{
  StmtBuilder stmt{_types, _variables};
  return director(stmt);
}

bool spirv::FunctionBuilder::declareUndefinedVar(const VarDecl& var)
{
  StmtBuilder stmt{_types, _variables};
  // Nothing to do here.
  // If the variable is loaded before being written to it produces an OpUndef value.
  return true;
}

template<class F>
bool spirv::FunctionBuilder::declareVar(const VarDecl& var, F director)
{
  StmtBuilder stmt{_types, _variables};
  // If the variable is loaded before being written to it produces an OpUndef value.
  // TODO: references
  if(director(stmt))
  {
    _variables.store({0, &var, {}}, _variables.load(stmt.expr()));
    return true;
  }
  return false;
}

template<class F>
bool spirv::FunctionBuilder::pushScope(F director)
{
  if(!_function)
  {
    // This is the first block in the function.
    // This means we know the entire signature and can create the type.
    assert(_returnType != 0 && "return type not yet set");

    auto params = std::vector<spv::Id>{};
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
    for(auto i = 0u; i < params.size(); ++i)
    {
      _builder.addName(_function->getParamId(i), _params[i]->getNameAsString().c_str());
      _variables.trackVariable(*_params[i], _function->getParamId(i));
    }
  }
  // We don't need any extra setup for a new scope, just continue the existing block.
  // It's an aspect of the frontend we don't have to care about.
  return director();
}

bool spirv::FunctionBuilder::setReturnType(QualType type)
{
  assert(_returnType == 0 && "return type already set");
  _returnType = _types[type];
  return true;
}

spv::Id spirv::FunctionBuilder::finalize()
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
