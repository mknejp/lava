//===--- ModuleBuilder.cpp - Common Code Gen Stuff --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Lava/ModuleBuilder.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Lava/IndentWriter.h"
#include "clang/lava/GLSL.h"

using namespace clang;
using namespace lava;

ModuleBuilder::Concept::~Concept() = default;
void RecordBuilder::vftbl() { }
void StmtBuilder::vftbl() { }
void FunctionBuilder::vftbl() { }

////////////////////////////////////////////////////////////////////////////////
// Visitors
//

namespace
{
  // Visitor for the possible top-level nodes of a function scope.
  class FunctionVisitor
  : public ConstStmtVisitor<FunctionVisitor>
  , public ConstDeclVisitor<FunctionVisitor>
  {
  public:
    using StmtVisitor = ConstStmtVisitor<FunctionVisitor>;
    using DeclVisitor = ConstDeclVisitor<FunctionVisitor>;

    FunctionVisitor(FunctionBuilder& builder) : _builder(builder) { }

    // Statements
    void VisitCompoundStmt(const CompoundStmt* stmt);
    void VisitDeclStmt(const DeclStmt* stmt);
    void VisitIfStmt(const IfStmt* stmt);
    void VisitReturnStmt(const ReturnStmt* stmt);

    // Declarations
    void VisitVarDecl(const VarDecl* decl);

    // Expressions
    void VisitExpr(const Expr* expr);

  private:
    FunctionBuilder& _builder;
  };

  // Visitor for all nodes that could be in an expression
  class ExprVisitor : public ConstStmtVisitor<ExprVisitor>
  {
  public:
    ExprVisitor(StmtBuilder& stmt) : _builder(stmt) { }

    void VisitBinaryOperator(const BinaryOperator* expr);
    void VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr* expr);
    void VisitFloatingLiteral(const FloatingLiteral* expr);
    void VisitIntegerLiteral(const IntegerLiteral* expr);
    void VisitParenExpr(const ParenExpr* expr);

  private:
    StmtBuilder& _builder;
  };

  // If the stament is not a CompoundStmt insert a new scope anyway.
  // Use this everywhere the source language allows a single statement without
  // enclosing braces. Since we may have to emit destructor calls or split up
  // multiple variable declarations into multiple statements this ensures the
  // target language generator has a way to group everything together.
  template<class F>
  void forceScope(const Stmt* stmt, FunctionBuilder& builder, F&& f)
  {
    if(dyn_cast<CompoundStmt>(stmt))
    {
      f(builder);
    }
    else
    {
      builder.pushScope(std::forward<F>(f));
    }
  }

  // Create a Callable<void(FunctionBuilder&)> that builds up a block using
  // its own private instance of FunctionVisitor.
  auto makeBlockBuilder = [] (const Stmt* stmt)
  {
    return [stmt] (FunctionBuilder& builder)
    {
      forceScope(stmt, builder, [stmt] (FunctionBuilder& builder)
      {
        FunctionVisitor{builder}.StmtVisitor::Visit(stmt);
      });
    };
  };
}

////////////////////////////////////////////////////////////////////////////////
// FunctionVisitor
//

void FunctionVisitor::VisitCompoundStmt(const CompoundStmt* stmt)
{
  _builder.pushScope([stmt] (FunctionBuilder& builder)
  {
    for(const auto* stmt : stmt->body())
    {
      FunctionVisitor{builder}.StmtVisitor::Visit(stmt);
    }
  });
}

void FunctionVisitor::VisitDeclStmt(const DeclStmt* stmt)
{
  for(const auto* decl : stmt->decls())
  {
    DeclVisitor::Visit(decl);
  }
}

void FunctionVisitor::VisitIfStmt(const IfStmt* stmt)
{
  auto build = [stmt] (FunctionBuilder& builder)
  {
    if(stmt->getElse())
    {
      builder.buildIfStmt([stmt] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(stmt->getCond()); },
                          makeBlockBuilder(stmt->getThen()),
                          makeBlockBuilder(stmt->getElse()));
    }
    else
    {
      builder.buildIfStmt([stmt] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(stmt->getCond()); },
                          makeBlockBuilder(stmt->getThen()));
    }
  };

  if(stmt->getConditionVariable())
  {
    // If the condition declares a variable open a new scope to ensure its name
    // doesn't clash with a variable that is already declared and makes it go
    // out of scope immediately following the if/then/else stmt.
    _builder.pushScope([&] (FunctionBuilder& builder)
    {
      FunctionVisitor{builder}.VisitDeclStmt(stmt->getConditionVariableDeclStmt());
      build(builder);
    });
  }
  else
  {
    build(_builder);
  }
}

void FunctionVisitor::VisitReturnStmt(const ReturnStmt* stmt)
{
  // TODO: run destructors
  _builder.buildReturnStmt([stmt, this] (StmtBuilder& builder)
  {
    if(auto* expr = stmt->getRetValue())
    {
      ExprVisitor{builder}.Visit(expr);
    }
  });
}

void FunctionVisitor::VisitVarDecl(const VarDecl* decl)
{
  if(!decl->isStaticLocal())
  {
    if(const auto* init = decl->getInit())
    {
      _builder.declareVar(*decl, [init] (StmtBuilder& builder)
      {
        ExprVisitor{builder}.Visit(init);
      });
    }
    else
    {
      _builder.declareUndefinedVar(*decl);
    }
  }
  else
    llvm_unreachable("function local statics not implemented");
}

void FunctionVisitor::VisitExpr(const Expr* expr)
{
  _builder.buildStmt([expr] (StmtBuilder& builder)
  {
    ExprVisitor{builder}.Visit(expr);
  });
}

////////////////////////////////////////////////////////////////////////////////
// ExprVisitor
//

void ExprVisitor::VisitBinaryOperator(const BinaryOperator* expr)
{
  _builder.emitBinaryOperator(*expr,
                              [expr] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(expr->getLHS()); },
                              [expr] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(expr->getRHS()); });
}

void ExprVisitor::VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr* expr)
{
  _builder.emitBooleanLiteral(*expr);
}

void ExprVisitor::VisitFloatingLiteral(const FloatingLiteral* expr)
{
  _builder.emitFloatingLiteral(*expr);
}

void ExprVisitor::VisitIntegerLiteral(const IntegerLiteral* expr)
{
  _builder.emitIntegerLiteral(*expr);
}

void ExprVisitor::VisitParenExpr(const ParenExpr* expr)
{
  _builder.emitParenExpr([expr] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(expr->getSubExpr()); });
}

////////////////////////////////////////////////////////////////////////////////
// buildModule
//

// TODO: Validate types against backend capabilities before sending stuff to print
// TODO: Backend-agnostic optimizations

namespace
{
  template<class EmittedEntities, class F>
  bool for_each_entity(ShaderStage stage, EmittedEntities& entities, F f)
  {
    for(auto& entity : entities)
    {
      if((entity.stages & stage) != ShaderStage::none)
      {
        if(!f(entity))
          return false;
      }
    }
    return true;
  }

  bool buildRecord(CXXRecordDecl& decl, RecordBuilder& builder)
  {
    auto baseIndex = 0u;
    for(const CXXBaseSpecifier& base : decl.bases())
    {
      builder.addBase(base.getType(), baseIndex++);
    }

    // Lambdas have both captures and fields, but the fields have no names?
    if(!decl.isLambda())
    {
      for(const FieldDecl* field : decl.fields())
      {
        builder.addField(field->getType(), field->getName());
      }
    }
    else
    {
      for(const LambdaCapture& capture : decl.captures())
      {
        assert(!capture.capturesThis() && "'this' captures not supported");
        assert(!(capture.getCaptureKind() & LCK_ByRef) && "captures by reference not supported");
        auto* var = capture.getCapturedVar();
        builder.addCapture(var->getType(), var->getName());
      }
    }
    return true;
  }

  bool buildRecords(ShaderContext& context, ModuleBuilder& module, ShaderStage stage)
  {
    return for_each_entity(stage, context.records, [&] (EmittedRecord& record)
    {
      return module.buildRecord(QualType(record.decl->getTypeForDecl(), 0), [&record] (RecordBuilder& builder)
      {
        buildRecord(*record.decl, builder);
      });
    });
  }

  void buildFunction(FunctionDecl& decl, FunctionBuilder& builder)
  {
    builder.setReturnType(decl.getReturnType());
    // We feed the arguments individually in case we have to transform them
    for(auto* param : decl.params())
    {
      builder.addParam(*param);
    }
    FunctionVisitor{builder}.StmtVisitor::Visit(decl.getBody());
  }

  void buildFunctions(ShaderContext& context, ModuleBuilder& module, ShaderStage stage)
  {
    for_each_entity(stage, context.functions, [&] (EmittedFunction& f)
    {
      return module.buildFunction(*f.decl, [&f] (FunctionBuilder& builder)
      {
        buildFunction(*f.decl, builder);
      });
    });
  }
}

bool clang::lava::buildModule(ShaderContext& context, ModuleBuilder& module, ShaderStage stage)
{
  buildRecords(context, module, stage);
  buildFunctions(context, module, stage);

  return true;
}
