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
#include "clang/Lava/CodePrintingTools.h"
#include "clang/Lava/ModuleBuilder.h"
#include "clang/Lava/IndentWriter.h"

using namespace clang;
using namespace lava;

using llvm::StringRef;

ModuleBuilder clang::lava::spirv::createModuleBuilder(ASTContext& ast)
{
  return lava::ModuleBuilder::create<spirv::ModuleBuilder>(ast);
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
      case BuiltinType::Kind::Float:
        return _builder.makeFloatType(32);
      case BuiltinType::Kind::Double:
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
// FunctionBuilder
//

spirv::FunctionBuilder::FunctionBuilder(FunctionDecl& decl, TypeCache& types, TypeMangler& mangler)
: _types(types)
, _mangler(mangler)
, _decl(decl)
{
}

bool spirv::FunctionBuilder::setReturnType(QualType type)
{
  assert(_returnType == 0 && "return type already set");
  _returnType = _types[type];
  return true;
}

bool spirv::FunctionBuilder::addParam(ParmVarDecl* param)
{
  _params.push_back(param);
  return true;
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
    }
  }
  // We don't need any extra setup for a new scope, just continue the existing block.
  // It's an aspect of the frontend we don't have to care about.
  return director();
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
