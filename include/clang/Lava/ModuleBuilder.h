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
  class CastExpr;
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
  bool emitBinaryOperator(const BinaryOperator& expr, LHS lhs, RHS rhs)
  {
    auto f1 = Director{std::forward<LHS>(lhs)};
    auto f2 = Director{std::forward<RHS>(rhs)};
    return _success = _success && emitBinaryOperatorImpl(expr, f1, f2);
  }
  bool emitBooleanLiteral(const CXXBoolLiteralExpr& expr)
  {
    return _success = _success && emitBooleanLiteralImpl(expr);
  }
  template<class F>
  bool emitCast(const CastExpr& expr, F&& subexpr)
  {
    auto f = Director{std::forward<F>(subexpr)};
    return _success = _success && emitCastImpl(expr, f);
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
  bool emitParenExpr(F&& subexpr)
  {
    auto f = Director{std::forward<F>(subexpr)};
    return _success = _success && emitParenExprImpl(f);
  }
  template<class F>
  bool emitUnaryOperator(const UnaryOperator& expr, F&& subexpr)
  {
    auto f = Director{std::forward<F>(subexpr)};
    return _success = _success && emitUnaryOperatorImpl(expr, f);
  }
  bool emitVariableAccess(const VarDecl& var)
  {
    return _success = _success && emitVariableAccessImpl(var);
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
  virtual bool emitCastImpl(const CastExpr& expr, Director& subexpr) = 0;
  virtual bool emitFloatingLiteralImpl(const FloatingLiteral& expr) = 0;
  virtual bool emitIntegerLiteralImpl(const IntegerLiteral& expr) = 0;
  virtual bool emitParenExprImpl(Director& subexpr) = 0;
  virtual bool emitUnaryOperatorImpl(const UnaryOperator& expr, Director& subexpr) = 0;
  virtual bool emitVariableAccessImpl(const VarDecl& var) = 0;

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
  bool emitCastImpl(const CastExpr& expr, Director& subexpr) override
  {
    return _target.emitCast(expr, Invoke{_target, subexpr});
  }
  bool emitFloatingLiteralImpl(const FloatingLiteral& expr) override
  {
    return _target.emitFloatingLiteral(expr);
  }
  bool emitIntegerLiteralImpl(const IntegerLiteral& expr) override
  {
    return _target.emitIntegerLiteral(expr);
  }
  virtual bool emitParenExprImpl(Director& subexpr) override
  {
    return _target.emitParenExpr(Invoke{_target, subexpr});
  }
  virtual bool emitUnaryOperatorImpl(const UnaryOperator& expr, Director& subexpr) override
  {
    return _target.emitUnaryOperator(expr, Invoke{_target, subexpr});
  }
  bool emitVariableAccessImpl(const VarDecl& var) override
  {
    return _target.emitVariableAccess(var);
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

  /// Build a break statement for either a loop of a swith.
  /// \param cleanup A function director responsible for generating cleanup code. It is called at a point where cleanup calls are allowed in the target language.
  template<class F>
  bool buildBreakStmt(F&& cleanup)
  {
    auto f = Director{std::forward<F>(cleanup)};
    return _success = _success && buildBreakStmtImpl(f);
  }
  /// Build a continue statement.
  /// \param cleanup A function director responsible for generating cleanup code. It is called at a point where cleanup calls are allowed in the target language.
  template<class F>
  bool buildContinueStmt(F&& cleanup)
  {
    auto f = Director{std::forward<F>(cleanup)};
    return _success = _success && buildContinueStmtImpl(f);
  }
  /// Build a do-while loop.
  /// \param cond A statement director to build up the condition expression.
  /// \param body An unary director to build up the content of the while loop's body.
  template<class F1, class F2>
  bool buildDoStmt(F1&& cond, F2&& body)
  {
    auto f1 = StmtDirector{std::forward<F1>(cond)};
    auto f2 = Director{std::forward<F2>(body)};
    return _success = _success && buildDoStmtImpl(f1, f2);
  }
  /// Build a for loop.
  /// \param hasCond Indicates whether the \p cond director produces any output. Some generators require this knowledge a priori and cannot rely solely on the code generated by the director.
  /// \param init A director to build up the init statement.
  /// \param cond A statement director to build up the condition expression.
  /// \param inc A statement director to build up the increment expression. Depending on the target language this director may be invoked multiple times. Once for the regular loop increment and once before each continue statement.
  /// \param body An unary director to build up the content of the for loop's body.
  template<class F1, class F2, class F3, class F4>
  bool buildForStmt(bool hasCond, F1&& init, F2&& cond, F3&& inc, F4&& body)
  {
    auto f1 = Director{std::forward<F1>(init)};
    auto f2 = StmtDirector{std::forward<F2>(cond)};
    auto f3 = StmtDirector{std::forward<F3>(inc)};
    auto f4 = Director{std::forward<F4>(body)};
    return _success = _success && buildForStmtImpl(hasCond, f1, f2, f3, f4);
  }
  /// Build an if statement without an else part.
  /// \param cond A statement director to build up the condition expression.
  /// \param then An unary director to build up the content of the then-block.
  template<class F1, class F2>
  bool buildIfStmt(F1&& cond, F2&& then)
  {
    auto f1 = StmtDirector{std::forward<F1>(cond)};
    auto f2 = Director{std::forward<F2>(then)};
    return _success = _success && buildIfStmtImpl(f1, f2);
  }
  /// Build an if statement with an else part.
  /// \param cond A statement director to build up the condition.
  /// \param then An unay director to build up the content of the then-block expression.
  /// \param orElse An unary director to build up the content of the else-block.
  template<class F1, class F2, class F3>
  bool buildIfStmt(F1&& cond, F2&& then, F3&& orElse)
  {
    auto f1 = StmtDirector{std::forward<F1>(cond)};
    auto f2 = Director{std::forward<F2>(then)};
    auto f3 = Director{std::forward<F3>(orElse)};
    return _success = _success && buildIfStmtImpl(f1, f2, f3);
  }
  /// Build the statement to return from a function.
  /// \param expr A statement director for building the expression making up the return value. The director must produce a value if the function does not return void. It is allowed to produce an expression even if the function returns void.
  /// \todo a cleanup director for inserting destructors at the correct place
  template<class F>
  bool buildReturnStmt(F&& expr)
  {
    auto f = StmtDirector{std::forward<F>(expr)};
    return _success = _success && buildReturnStmtImpl(f);
  }
  /// Build a new statement that is not the declaration of a new variable
  /// \param stmt A statement directo to build the actual statement/expression.
  template<class F>
  bool buildStmt(F&& stmt)
  {
    auto f = StmtDirector{std::forward<F>(stmt)};
    return _success = _success && buildStmtImpl(f);
  }
  /// Build a while loop.
  /// \param cond A statement director to build up the condition expression.
  /// \param body An unary director to build up the content of the while loop's body.
  template<class F1, class F2>
  bool buildWhileStmt(F1&& cond, F2&& body)
  {
    auto f1 = StmtDirector{std::forward<F1>(cond)};
    auto f2 = Director{std::forward<F2>(body)};
    return _success = _success && buildWhileStmtImpl(f1, f2);
  }
  /// Declare a single new local variable with no definition.
  bool declareUndefinedVar(const VarDecl& var)
  {
    return _success = _success && declareUndefinedVarImpl(var);
  }
  /// Declare a single new local variable with an initial value.
  /// \param init a statement director to build the expression to initialize the variable with.
  template<class F>
  bool declareVar(const VarDecl& var, F&& init)
  {
    auto f = StmtDirector{std::forward<F>(init)};
    return _success = _success && declareVarImpl(var, f);
  }
  /// Open a new local scope.
  /// At least one scope must be opened for a function that is not imported, even if it is trivial.
  /// \param scope An unary director to build the nested scope constent.
  template<class F>
  bool pushScope(F&& scope)
  {
    auto f = Director{std::forward<F>(scope)};
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
  virtual bool buildBreakStmtImpl(Director& cleanup) = 0;
  virtual bool buildContinueStmtImpl(Director& cleanup) = 0;
  virtual bool buildDoStmtImpl(StmtDirector& cond, Director& body) = 0;
  virtual bool buildForStmtImpl(bool hasCond, Director& init, StmtDirector& cond,
                                StmtDirector& inc, Director& body) = 0;
  virtual bool buildIfStmtImpl(StmtDirector& cond, Director& then) = 0;
  virtual bool buildIfStmtImpl(StmtDirector& cond, Director& then, Director& orElse) = 0;
  virtual bool buildReturnStmtImpl(StmtDirector& expr) = 0;
  virtual bool buildStmtImpl(StmtDirector& director) = 0;
  virtual bool buildWhileStmtImpl(StmtDirector& cond, Director& body) = 0;
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
  bool buildBreakStmtImpl(Director& cleanup) override
  {
    return _target.buildBreakStmt(DirectorInvoker<T, FunctionBuilder>{_target, cleanup});
  }
  bool buildContinueStmtImpl(Director& cleanup) override
  {
    return _target.buildContinueStmt(DirectorInvoker<T, FunctionBuilder>{_target, cleanup});
  }
  bool buildDoStmtImpl(StmtDirector& cond, Director& body) override
  {
    return _target.buildDoStmt(DirectorInvoker<T, StmtBuilder>{_target, cond},
                               DirectorInvoker<T, FunctionBuilder>{_target, body});
  }
  bool buildForStmtImpl(bool hasCond, Director& init, StmtDirector& cond,
                        StmtDirector& inc, Director& body) override
  {
    return _target.buildForStmt(hasCond,
                                DirectorInvoker<T, FunctionBuilder>{_target, init},
                                DirectorInvoker<T, StmtBuilder>{_target, cond},
                                DirectorInvoker<T, StmtBuilder>{_target, inc},
                                DirectorInvoker<T, FunctionBuilder>{_target, body});
  }
  bool buildIfStmtImpl(StmtDirector& cond, Director& then, Director& orElse) override
  {
    return _target.buildIfStmt(DirectorInvoker<T, StmtBuilder>{_target, cond},
                               DirectorInvoker<T, FunctionBuilder>{_target, then},
                               DirectorInvoker<T, FunctionBuilder>{_target, orElse});
  }
  bool buildIfStmtImpl(StmtDirector& cond, Director& then) override
  {
    return _target.buildIfStmt(DirectorInvoker<T, StmtBuilder>{_target, cond},
                               DirectorInvoker<T, FunctionBuilder>{_target, then});
  }
  bool buildReturnStmtImpl(StmtDirector& expr) override
  {
    return _target.buildReturnStmt(DirectorInvoker<T, StmtBuilder>{_target, expr});
  }
  bool buildStmtImpl(StmtDirector& stmt) override
  {
    return _target.buildStmt(DirectorInvoker<T, StmtBuilder>{_target, stmt});
  }
  bool buildWhileStmtImpl(StmtDirector& cond, Director& body) override
  {
    return _target.buildWhileStmt(DirectorInvoker<T, StmtBuilder>{_target, cond},
                                  DirectorInvoker<T, FunctionBuilder>{_target, body});
  }
  bool declareUndefinedVarImpl(const VarDecl& var) override
  {
    return _target.declareUndefinedVar(var);
  }
  bool declareVarImpl(const VarDecl& var, StmtDirector& init) override
  {
    return _target.declareVar(var, DirectorInvoker<T, StmtBuilder>{_target, init});
  }
  bool pushScopeImpl(Director& scope) override
  {
    return _target.pushScope([this, &scope] { scope(*this); return success(); });
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
