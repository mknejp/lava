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

private:
  template<class T>
  class Impl;
  friend ModuleBuilder;

  bool success() const { return _success; }

  virtual auto addBaseImpl(QualType type, unsigned index) -> bool = 0;
  virtual auto addFieldImpl(QualType type, llvm::StringRef identifier) -> bool = 0;
  virtual auto addCaptureImpl(QualType type, llvm::StringRef identifier) -> bool = 0;

  virtual void vftbl();

  bool _success = true;
};

template<class T>
class clang::lava::RecordBuilder::Impl final : public RecordBuilder
{
public:
  Impl(T& target) : _target(target) { }

private:
  auto addBaseImpl(QualType type, unsigned index) -> bool final { return _target.addBase(type, index); }
  auto addFieldImpl(QualType type, llvm::StringRef identifier) -> bool final { return _target.addField(type, identifier); }
  auto addCaptureImpl(QualType type, llvm::StringRef identifier) -> bool final { return _target.addCapture(type, identifier); }

  T& _target;
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
  auto buildRecord(QualType type, F&& director) -> bool
  {
    auto f = std::function<void(RecordBuilder&)>{std::forward<F>(director)};
    return _target->buildRecord(type, f);
  }
  // Can be text or binary and is the full module content to be written to a file
  auto moduleContent() -> std::string { return _target->moduleContent(); }

private:
  class Concept
  {
  public:
    virtual ~Concept();

    virtual auto buildRecord(QualType type, std::function<void(RecordBuilder&)>& director) -> bool = 0;
    virtual auto moduleContent() -> std::string = 0;
  };

  template<class T>
  class Model;

  ModuleBuilder(std::unique_ptr<Concept> ptr) : _target(std::move(ptr)) { }

  std::unique_ptr<Concept> _target;
};

template<class T>
class clang::lava::ModuleBuilder::Model : public Concept
{
public:
  template<class... Args>
  Model(Args&&... args) : _target(std::forward<Args>(args)...) { }

  auto buildRecord(QualType type, std::function<void(RecordBuilder&)>& director) -> bool override
  {
    return _target.buildRecord(type, DirectorInvoker<RecordBuilder>{_target, director});
  }

  auto moduleContent() -> std::string override { return _target.moduleContent(); }

private:
  template<class Builder>
  struct DirectorInvoker
  {
    T& target;
    std::function<void(Builder&)>& director;

    template<class RealBuilder>
    bool operator()(RealBuilder& builder) const
    {
      typename Builder::template Impl<RealBuilder> b{builder};
      director(b);
      return b.success();
    }
  };

  T _target;
};

#endif // LLVM_CLANG_LAVA_MODULEBUILDER_H
