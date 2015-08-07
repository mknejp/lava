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
#include "clang/Lava/ModuleBuilder.h"

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
// ModuleBuilder
//

spirv::ModuleBuilder::ModuleBuilder(ASTContext& ast)
: _ast(ast)
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
