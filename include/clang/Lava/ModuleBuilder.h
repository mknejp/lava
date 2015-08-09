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
  class BinaryOperator;
  class CXXBoolLiteralExpr;
  class DiagnosticsEngine;
  class FloatingLiteral;
  class IntegerLiteral;
  class ParmVarDecl;
  class UnaryOperator;

  namespace lava
  {
    class FunctionBuilder;
    class ModuleBuilder;
    class RecordBuilder;
    class StmtBuilder;

    template<class Target, class Builder>
    struct DirectorInvoker;

    bool buildModule(ShaderContext& context, ModuleBuilder& builder, ShaderStage stage);

  } // end namespace lava
} // end namespace clang

// This is used everywhere a method of an actual builder implementation has to
// create a new child builder on the stack and pass it as argument to the user-
// provided director callback taking the type-erased base class for the new
// child builder. This pattern allows us to save lots of allocations and enables
// a "recursive descend"-like code generation model.
template<class Target, class Builder>
struct clang::lava::DirectorInvoker
{
  Target& target;
  std::function<void(Builder&)>& director;

  template<class RealBuilder>
  bool operator()(RealBuilder& builder) const
  {
    typename Builder::template Impl<RealBuilder> b{builder};
    director(b);
    return b.success();
  }
};

////////////////////////////////////////////////////////////////////////////////
// RecordBuilder
//

class clang::lava::RecordBuilder
{
public:
  bool addBase(QualType type, unsigned index)
  {
    return _success && addBaseImpl(type, index);
  }
  bool addField(QualType type, llvm::StringRef identifier)
  {
    return _success && addFieldImpl(type, identifier);
  }
  bool addCapture(QualType type, llvm::StringRef identifier)
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
  template<class Target, class Director>
  friend struct DirectorInvoker;

  bool success() const { return _success; }

  virtual bool addBaseImpl(QualType type, unsigned index) = 0;
  virtual bool addFieldImpl(QualType type, llvm::StringRef identifier) = 0;
  virtual bool addCaptureImpl(QualType type, llvm::StringRef identifier) = 0;

  virtual void vftbl();

  bool _success = true;
};

template<class T>
class clang::lava::RecordBuilder::Impl final : public RecordBuilder
{
public:
  Impl(T& target) : _target(target) { }

private:
  bool addBaseImpl(QualType type, unsigned index) override { return _target.addBase(type, index); }
  bool addFieldImpl(QualType type, llvm::StringRef identifier) override { return _target.addField(type, identifier); }
  bool addCaptureImpl(QualType type, llvm::StringRef identifier) override { return _target.addCapture(type, identifier); }

  T& _target;
};

////////////////////////////////////////////////////////////////////////////////
// StmtBuilder
//

class clang::lava::StmtBuilder
{
  using Director = std::function<void(StmtBuilder&)>;

public:
  template<class LHS, class RHS>
  bool emitBinaryOperator(const BinaryOperator& expr, LHS lhsDirector, RHS rhsDirector)
  {
    auto lhs = Director{std::forward<LHS>(lhsDirector)};
    auto rhs = Director{std::forward<RHS>(rhsDirector)};
    return _success = _success && emitBinaryOperatorImpl(expr, lhs, rhs);
  }
  bool emitBooleanLiteral(const CXXBoolLiteralExpr& expr)
  {
    return _success = _success && emitBooleanLiteralImpl(expr);
  }
  bool emitFloatingLiteral(const FloatingLiteral& expr)
  {
    return _success = _success && emitFloatingLiteralImpl(expr);
  }
  bool emitIntegerLiteral(const IntegerLiteral& expr)
  {
    return _success = _success && emitIntegerLiteralImpl(expr);
  }
  template<class F>
  bool emitUnaryOperator(const UnaryOperator& expr, F&& subexpr)
  {
    auto f = Director{std::forward<F>(subexpr)};
    return _success = _success && emitUnaryOperatorImpl(expr, f);
  }

protected:
  StmtBuilder() = default;
  ~StmtBuilder() = default;
  StmtBuilder(const StmtBuilder&) = default;
  StmtBuilder(StmtBuilder&&) = default;
  StmtBuilder& operator=(const StmtBuilder&) = default;
  StmtBuilder& operator=(StmtBuilder&&) = default;

private:
  template<class T>
  class Impl;
  template<class Target, class Director>
  friend struct DirectorInvoker;

  bool success() const { return _success; }

  virtual bool emitBinaryOperatorImpl(const BinaryOperator& expr, Director& lhs, Director& rhs) = 0;
  virtual bool emitBooleanLiteralImpl(const CXXBoolLiteralExpr& expr) = 0;
  virtual bool emitFloatingLiteralImpl(const FloatingLiteral& expr) = 0;
  virtual bool emitIntegerLiteralImpl(const IntegerLiteral& expr) = 0;
  virtual bool emitUnaryOperatorImpl(const UnaryOperator& expr, Director& subexpr) = 0;

  virtual void vftbl();

  bool _success = true;
};

template<class T>
class clang::lava::StmtBuilder::Impl final : public StmtBuilder
{
public:
  Impl(T& target) : _target(target) { }

private:
  using Invoke = DirectorInvoker<T, StmtBuilder>;

  bool emitBinaryOperatorImpl(const BinaryOperator& expr, Director& lhs, Director& rhs) override
  {
    return _target.emitBinaryOperator(expr, Invoke{_target, lhs}, Invoke{_target, rhs});
  }
  bool emitBooleanLiteralImpl(const CXXBoolLiteralExpr& expr) override
  {
    return _target.emitBooleanLiteral(expr);
  }
  bool emitFloatingLiteralImpl(const FloatingLiteral& expr) override
  {
    return _target.emitFloatingLiteral(expr);
  }
  bool emitIntegerLiteralImpl(const IntegerLiteral& expr) override
  {
    return _target.emitIntegerLiteral(expr);
  }
  virtual bool emitUnaryOperatorImpl(const UnaryOperator& expr, Director& subexpr) override
  {
    return _target.emitUnaryOperator(expr, Invoke{_target, subexpr});
  }

  T& _target;
};

////////////////////////////////////////////////////////////////////////////////
// FunctionBuilder
//

class clang::lava::FunctionBuilder
{
  using Director = std::function<void(FunctionBuilder&)>;
  using StmtDirector = std::function<void(StmtBuilder&)>;
public:
  /// \name Function header setup
  /// @{

  /// Specify the function's return type (must be called exactly once before opening the function scope)
  bool setReturnType(QualType type)
  {
    return _success = _success && setReturnTypeImpl(type);
  }
  /// Add a new formal parameter to the function, optional.
  bool addParam(const ParmVarDecl& param)
  {
    return _success = _success && addParamImpl(param);
  }

  /// @}
  /// \name Concent building
  /// @{

  /// Build a new statement that is not the declaration of a new variable
  template<class F>
  bool buildStmt(F&& director)
  {
    auto f = StmtDirector{std::forward<F>(director)};
    return _success = _success && buildStmtImpl(f);
  }
  /// Declare a single new local variable with no definition.
  bool declareUndefinedVar(const VarDecl& var)
  {
    return _success = _success && declareUndefinedVarImpl(var);
  }
  /// Declare a single new local variable with its definition provided by a statement built by \p director.
  template<class F>
  bool declareVar(const VarDecl& var, F&& director)
  {
    auto f = StmtDirector{std::forward<F>(director)};
    return _success = _success && declareVarImpl(var, f);
  }
  /// Open a new local scope.
  /// At least one scope must be opened for a function that is not imported, even if it is trivial.
  template<class F>
  bool pushScope(F&& director)
  {
    auto f = Director{std::forward<F>(director)};
    return _success = _success && pushScopeImpl(f);
  }

  /// @}

protected:
  FunctionBuilder() = default;
  ~FunctionBuilder() = default;
  FunctionBuilder(const FunctionBuilder&) = default;
  FunctionBuilder(FunctionBuilder&&) = default;
  FunctionBuilder& operator=(const FunctionBuilder&) = default;
  FunctionBuilder& operator=(FunctionBuilder&&) = default;

private:
  template<class T>
  class Impl;
  template<class Target, class Director>
  friend struct DirectorInvoker;

  bool success() const { return _success; }

  virtual bool addParamImpl(const ParmVarDecl& param) = 0;
  virtual bool buildStmtImpl(StmtDirector& director) = 0;
  virtual bool declareUndefinedVarImpl(const VarDecl& var) = 0;
  virtual bool declareVarImpl(const VarDecl& var, StmtDirector& director) = 0;
  virtual bool pushScopeImpl(Director& director) = 0;
  virtual bool setReturnTypeImpl(QualType type) = 0;

  virtual void vftbl();

  bool _success = true;
};

template<class T>
class clang::lava::FunctionBuilder::Impl final : public FunctionBuilder
{
public:
  Impl(T& target) : _target(target) { }

private:
  bool addParamImpl(const ParmVarDecl& param) override
  {
    return _target.addParam(param);
  }
  bool buildStmtImpl(StmtDirector& director) override
  {
    return _target.buildStmt(DirectorInvoker<T, StmtBuilder>{_target, director});
  }
  bool declareUndefinedVarImpl(const VarDecl& var) override
  {
    return _target.declareUndefinedVar(var);
  }
  bool declareVarImpl(const VarDecl& var, StmtDirector& director) override
  {
    return _target.declareVar(var, DirectorInvoker<T, StmtBuilder>{_target, director});
  }
  bool pushScopeImpl(Director& director) override
  {
    return _target.pushScope([this, &director] { director(*this); return success(); });
  }
  bool setReturnTypeImpl(QualType type) override
  {
    return _target.setReturnType(type);
  }

  T& _target;
};

////////////////////////////////////////////////////////////////////////////////
// ModuleBuilder
//

class clang::lava::ModuleBuilder
{
  using RecordDirector = std::function<void(RecordBuilder&)>;
  using FunctionDirector = std::function<void(FunctionBuilder&)>;

public:
  template<class T, class... Args>
  static ModuleBuilder create(Args&&... args)
  {
    return ModuleBuilder{llvm::make_unique<Model<T>>(std::forward<Args>(args)...)};
  }

  template<class F>
  auto buildRecord(QualType type, F&& director) -> bool
  {
    auto f = RecordDirector{std::forward<F>(director)};
    return _target->buildRecord(type, f);
  }
  template<class F>
  auto buildFunction(FunctionDecl& decl, F&& director) -> bool
  {
    auto f = FunctionDirector{std::forward<F>(director)};
    return _target->buildFunction(decl, f);
  }
  // Can be text or binary and is the full module content to be written to a file
  auto moduleContent() -> std::string { return _target->moduleContent(); }

private:
  class Concept
  {
  public:
    virtual ~Concept();

    virtual auto buildRecord(QualType type, RecordDirector& director) -> bool = 0;
    virtual auto buildFunction(FunctionDecl& decl, FunctionDirector& director) -> bool = 0;
    virtual auto moduleContent() -> std::string = 0;
  };

  template<class T>
  class Model;

  ModuleBuilder(std::unique_ptr<Concept> ptr) : _target(std::move(ptr)) { }

  std::unique_ptr<Concept> _target;
};

template<class T>
class clang::lava::ModuleBuilder::Model final : public Concept
{
public:
  template<class... Args>
  Model(Args&&... args) : _target(std::forward<Args>(args)...) { }

  auto buildRecord(QualType type, RecordDirector& director) -> bool override
  {
    return _target.buildRecord(type, DirectorInvoker<T, RecordBuilder>{_target, director});
  }

  auto buildFunction(FunctionDecl& decl, FunctionDirector& director) -> bool override
  {
    return _target.buildFunction(decl, DirectorInvoker<T, FunctionBuilder>{_target, director});
  }

  auto moduleContent() -> std::string override { return _target.moduleContent(); }

  T _target;
};

#endif // LLVM_CLANG_LAVA_MODULEBUILDER_H
