//===--- LavaAction.cpp - Lava frontend actions -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Lava/ShaderContext.h"

#include "clang/lava/IndentWriter.h"

using namespace clang;
using namespace lava;

ShaderTranslator::~ShaderTranslator() = default;

////////////////////////////////////////////////////////////////////////////////
// shared shader properties
//

// All of GLSL, HLSL, Metal have basically the same properties for these
namespace
{
  bool isValidVectorDimension(unsigned dim)
  {
    switch (dim)
    {
      case 2:
      case 3:
      case 4:
        return true;
      default:
        return false;
    }
  }

  bool isValidBuiltinType(const BuiltinType& type)
  {
    switch(type.getKind())
    {
      case BuiltinType::Bool:
      case BuiltinType::Int:
      case BuiltinType::UInt:
      case BuiltinType::Float:
        return true;

      default:
        return false;
    }
  }

  void printVectorDimension(llvm::raw_ostream& o, unsigned dim)
  {
    assert(isValidVectorDimension(dim));
    switch(dim)
    {
      case 2: o << '2'; break;
      case 3: o << '3'; break;
      case 4: o << '4'; break;
    }
  }

  void printVector(IndentWriter& w, llvm::StringRef name, unsigned dim)
  {
    w << name;
    printVectorDimension(w.ostream(), dim);
  }

  void printMatrix(IndentWriter& w, llvm::StringRef name, unsigned first, unsigned second)
  {
    printVector(w, name, first);
    printVector(w, "x", second);
  }

  llvm::StringRef builtinTypeName(const BuiltinType& type)
  {
    assert(isValidBuiltinType(type));
    switch(type.getKind())
    {
      case BuiltinType::Bool:  return "bool";
      case BuiltinType::Int:   return "int";
      case BuiltinType::UInt:  return "uint";
      case BuiltinType::Float: return "float";
      default:
        llvm_unreachable("invalid builtin type");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// GLSL shader contexts
//

namespace
{
  class GLSL final : public ShaderTranslator
  {
  public:
    bool isValidBulitinType(const BuiltinType& type) const override;
    bool isValidMatrixElementType(QualType type) const override;
    bool isValidVectorDimension(unsigned dim) const override;
    bool isValidVectorElementType(QualType type) const override;

    void printType(IndentWriter& w, const BuiltinType& type) const override;
    void printType(IndentWriter& w, const ExtVectorType& type) const override;
    void printType(IndentWriter& w, const MatrixType& type) const override;

  private:
  };
}

std::unique_ptr<ShaderTranslator> clang::lava::createGLSLShaderTranslator()
{
  return llvm::make_unique<GLSL>();
}

bool GLSL::isValidBulitinType(const BuiltinType& type) const
{
  return ::isValidBuiltinType(type);
}

bool GLSL::isValidMatrixElementType(QualType type) const
{
  auto* builtin = type->getAs<BuiltinType>();
  if(!builtin)
    return false;

  return builtin->getKind() == BuiltinType::Float;
}

bool GLSL::isValidVectorDimension(unsigned dim) const
{
  return ::isValidVectorDimension(dim);
}

bool GLSL::isValidVectorElementType(QualType type) const
{
  auto* builtin = type->getAs<BuiltinType>();
  if(!builtin)
    return false;

  return isValidBulitinType(*builtin);
}

void GLSL::printType(IndentWriter& w, const BuiltinType& type) const
{
  w << ::builtinTypeName(type);
}

void GLSL::printType(IndentWriter& w, const ExtVectorType& type) const
{
  assert(isValidVectorElementType(type.getElementType()));

  auto builtin = type.getElementType()->getAs<BuiltinType>();
  StringRef name;
  switch(builtin->getKind())
  {
    case clang::BuiltinType::Bool:  name = "bvec"; break;
    case clang::BuiltinType::Int:   name = "ivec"; break;
    case clang::BuiltinType::UInt:  name = "uvec"; break;
    case clang::BuiltinType::Float: name = "vec"; break;
    default:
      llvm_unreachable("invalid vector element type");
  }

  printVector(w, name, type.getNumElements());
}

void GLSL::printType(IndentWriter& w, const MatrixType& type) const
{
  assert(isValidMatrixElementType(type.getElementType()));

  auto builtin = type.getElementType()->getAs<BuiltinType>();
  StringRef name;
  switch(builtin->getKind())
  {
    case clang::BuiltinType::Float: name = "mat"; break;
    default:
      llvm_unreachable("invalid matrix element type");
  }
  
  // GLSL matrix notation is column first
  printMatrix(w, name, type.getNumColumns(), type.getNumRows());
}
