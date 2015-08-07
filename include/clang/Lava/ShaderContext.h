//===--- LavaFrontendAction.h - Lava information gathering ------*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_SHADERCONTEXT_H
#define LLVM_CLANG_LAVA_SHADERCONTEXT_H

#include "clang/AST/Type.h"
#include "llvm/ADT/StringMap.h"
#include <memory>

namespace clang
{
  class Decl;
  class FunctionDecl;
  class TypeDecl;
  class VarDecl;

namespace lava
{
  class IndentWriter;

  enum class ShaderStage : unsigned
  {
    none,
    vertex   = (1 << 0),
    fragment = (1 << 1),

    all = vertex | fragment,
  };

  constexpr ShaderStage operator|(ShaderStage lhs, ShaderStage rhs) noexcept
  {
    return static_cast<ShaderStage>(static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
  }
  
  inline ShaderStage operator&(ShaderStage lhs, ShaderStage rhs) noexcept
  {
    return static_cast<ShaderStage>(static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs));
  }

  constexpr ShaderStage& operator|=(ShaderStage& lhs, ShaderStage rhs) noexcept
  {
    return lhs = lhs | rhs;
  }

  template<class Decl>
  struct UsedDecl
  {
    Decl* decl;
    ShaderStage stages;
    std::string emittedDecl;
    std::string emittedDef;
  };

  struct EmittedRecord
  {
    CXXRecordDecl* decl;
    ShaderStage stages;
  };

  struct EmittedFunction
  {
    FunctionDecl* decl;
    ShaderStage stages;
  };
  
  struct ShaderContext
  {
    std::vector<EmittedRecord> records;
    std::vector<EmittedFunction> functions;
    llvm::StringMap<UsedDecl<EnumDecl>> enums;
    llvm::StringMap<UsedDecl<TypeDecl>> types;
    llvm::StringMap<UsedDecl<VarDecl>> vars;
    llvm::StringMap<UsedDecl<FunctionDecl>> funcs;

    FunctionDecl* vertexFunction = nullptr;
    FunctionDecl* fragmentFunction = nullptr;
  };

  class ShaderTranslator
  {
  public:
    virtual ~ShaderTranslator();

    virtual bool isValidBulitinType(const BuiltinType& type) const = 0;
    virtual bool isValidMatrixElementType(QualType type) const = 0;
    virtual bool isValidVectorDimension(unsigned dim) const = 0;
    virtual bool isValidVectorElementType(QualType type) const = 0;

    virtual void printType(IndentWriter& w, const BuiltinType& type) const = 0;
    virtual void printType(IndentWriter& w, const ExtVectorType& type) const = 0;
    virtual void printType(IndentWriter& w, const MatrixType& type) const = 0;

  private:
  };

  std::unique_ptr<ShaderTranslator> createGLSLShaderTranslator();

} // end namespace lava
} // end namespace clang

#endif // LLVM_CLANG_LAVA_SHADERCONTEXT_H
