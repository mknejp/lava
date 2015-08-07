//===--- ModuleBuilder.h - Interface for Shader Module Builders -*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_MODULEBUILDER_H
#define LLVM_CLANG_LAVA_MODULEBUILDER_H

#include "clang/lava/ShaderContext.h"
#include <memory>

namespace clang
{
  class ASTContext;
  class DiagnosticsEngine;

  namespace lava
  {
    class FunctionBuilder;
    class ModuleBuilder;
    class RecordBuilder;
    template<class Builder>
    class RecordBuilderImpl;

    bool buildModule(ShaderContext& context, ModuleBuilder& builder, ShaderStage stage);
    
  } // end namespace lava
} // end namespace clang

////////////////////////////////////////////////////////////////////////////////
// RecordBuilder
//

class clang::lava::RecordBuilder
{
public:
  auto addBase(QualType type, unsigned index) -> bool
  {
    return _success && addBaseImpl(type, index);
  }
  auto addField(QualType type, llvm::StringRef identifier) -> bool
  {
    return _success && addFieldImpl(type, identifier);
  }
  auto addCapture(QualType type, llvm::StringRef identifier) -> bool
  {
    return _success && addCaptureImpl(type, identifier);
  }

protected:
  RecordBuilder() = default;
  ~RecordBuilder() = default;
  RecordBuilder(const RecordBuilder&) = default;
  RecordBuilder(RecordBuilder&&) = default;
  RecordBuilder& operator=(const RecordBuilder&) = default;
  RecordBuilder& operator=(RecordBuilder&&) = default;

  bool success() const { return _success; }

private:
  virtual auto addBaseImpl(QualType type, unsigned index) -> bool = 0;
  virtual auto addFieldImpl(QualType type, llvm::StringRef identifier) -> bool = 0;
  virtual auto addCaptureImpl(QualType type, llvm::StringRef identifier) -> bool = 0;

  virtual void vftbl();

  bool _success = true;
};

template<class T>
class clang::lava::RecordBuilderImpl final : public RecordBuilder
{
public:
  template<class... Args>
  RecordBuilderImpl(Args&&... args) : _theBuilder(std::forward<Args>(args)...) { }

  T& operator*() { return _theBuilder; }
  T* operator->() { return &_theBuilder; }
  using RecordBuilder::success;

private:
  auto addBaseImpl(QualType type, unsigned index) -> bool final { return _theBuilder.addBase(type, index); }
  auto addFieldImpl(QualType type, llvm::StringRef identifier) -> bool final { return _theBuilder.addField(type, identifier); }
  auto addCaptureImpl(QualType type, llvm::StringRef identifier) -> bool final { return _theBuilder.addCapture(type, identifier); }

  T _theBuilder;
};

////////////////////////////////////////////////////////////////////////////////
// ModuleBuilder
//

class clang::lava::ModuleBuilder
{
public:
  template<class T, class... Args>
  static ModuleBuilder create(Args&&... args)
  {
    return ModuleBuilder{llvm::make_unique<Model<T>>(std::forward<Args>(args)...)};
  }

  template<class F>
  auto buildRecord(QualType type, F&& blueprint) -> bool
  {
    auto f = std::function<void(RecordBuilder&)>{std::forward<F>(blueprint)};
    return _target->buildRecord(type, f);
  }
//  auto beginFunction(FunctionDecl& decl) -> FunctionBuilder& { return _target->beginFunction(decl); }
  // Can be text or binary and is the full module content to be written to a file
  auto moduleContent() -> std::string { return _target->moduleContent(); }

private:
  struct Concept
  {
    virtual ~Concept();

    virtual auto buildRecord(QualType type, std::function<void(RecordBuilder&)>& f) -> bool = 0;
//    virtual auto beginFunction(FunctionDecl& decl) -> FunctionBuilder& = 0;
    virtual auto moduleContent() -> std::string = 0;
  };

  template<class T>
  struct Model : Concept
  {
    template<class... Args>
    Model(Args&&... args) : target(std::forward<Args>(args)...) { }
    T target;

    auto buildRecord(QualType type, std::function<void(RecordBuilder&)>& f) -> bool override
    {
      return target.buildRecord(type, f);
    }
//    auto beginFunction(FunctionDecl& decl) -> FunctionBuilder& override { return target.beginFunction(decl); }
    auto moduleContent() -> std::string override { return target.moduleContent(); }
  };

  ModuleBuilder(std::unique_ptr<Concept> ptr) : _target(std::move(ptr)) { }

  std::unique_ptr<Concept> _target;
};

#endif // LLVM_CLANG_LAVA_MODULEBUILDER_H
