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

    FunctionVisitor(ASTContext& ast, FunctionBuilder& builder)
    : _ast(ast), _builder(builder) { }

    // Statements
    void VisitBreakStmt(const BreakStmt* stmt);
    void VisitCaseStmt(const CaseStmt* stmt);
    void VisitCompoundStmt(const CompoundStmt* stmt);
    void VisitContinueStmt(const ContinueStmt* stmt);
    void VisitDeclStmt(const DeclStmt* stmt);
    void VisitDefaultStmt(const DefaultStmt* stmt);
    void VisitDoStmt(const DoStmt* stmt);
    void VisitForStmt(const ForStmt* stmt);
    void VisitIfStmt(const IfStmt* stmt);
    void VisitReturnStmt(const ReturnStmt* stmt);
    void VisitSwitchStmt(const SwitchStmt* stmt);
    void VisitWhileStmt(const WhileStmt* stmt);

    // Declarations
    void VisitVarDecl(const VarDecl* decl);

    // Expressions
    void VisitExpr(const Expr* expr);

  private:
    template<class F>
    void buildStmtWithPossibleConditionVariable(VarDecl* conditionVariable,
                                                const DeclStmt* conditionVariableDeclStmt,
                                                F build);
    ASTContext& _ast;
    FunctionBuilder& _builder;
  };

  // Visitor for all nodes that could be in an expression
  class ExprVisitor : public ConstStmtVisitor<ExprVisitor>
  {
  public:
    ExprVisitor(StmtBuilder& stmt) : _builder(stmt) { }

    void VisitBinaryOperator(const BinaryOperator* expr);
    void VisitCastExpr(const CastExpr* expr);
    void VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr* expr);
    void VisitDeclRefExpr(const DeclRefExpr* expr);
    void VisitFloatingLiteral(const FloatingLiteral* expr);
    void VisitIntegerLiteral(const IntegerLiteral* expr);
    void VisitParenExpr(const ParenExpr* expr);
    void VisitUnaryOperator(const UnaryOperator* expr);

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

  // Create a Callable<void(FunctionBuilder&)> that builds up a block.
  auto makeBlockBuilder = [] (FunctionVisitor& fv, const Stmt* stmt)
  {
    return [stmt, &fv] (FunctionBuilder& builder)
    {
      forceScope(stmt, builder, [stmt, &fv] (FunctionBuilder& builder)
      {
        fv.StmtVisitor::Visit(stmt);
      });
    };
  };

  // Create a Callable<void(StmtBuilder&)> that builds up a an expression using
  // its own private instance of ExprVisitor.
  auto makeExprBuilder = [] (const Expr* expr)
  {
    return [expr] (StmtBuilder& builder)
    {
      if(expr)
      {
        ExprVisitor{builder}.Visit(expr);
      }
    };
  };
}

////////////////////////////////////////////////////////////////////////////////
// FunctionVisitor
//

void FunctionVisitor::VisitBreakStmt(const BreakStmt* stmt)
{
  _builder.buildBreakStmt([this] (FunctionBuilder& builder)
  {
    // TODO: local cleanup
  });
}

void FunctionVisitor::VisitCaseStmt(const CaseStmt* stmt)
{
  llvm::APSInt value;
  if(stmt->getLHS()->EvaluateAsInt(value, _ast))
  {
    _builder.buildSwitchCaseStmt(value.extOrTrunc(32));
    StmtVisitor::Visit(stmt->getSubStmt());
  }
}

void FunctionVisitor::VisitCompoundStmt(const CompoundStmt* stmt)
{
  _builder.pushScope([stmt, this] (FunctionBuilder& builder)
  {
    for(const auto* stmt : stmt->body())
    {
      StmtVisitor::Visit(stmt);
    }
  });
}

void FunctionVisitor::VisitContinueStmt(const ContinueStmt* stmt)
{
  _builder.buildContinueStmt([this] (FunctionBuilder& builder)
  {
    // TODO: local cleanup
  });
}

void FunctionVisitor::VisitDeclStmt(const DeclStmt* stmt)
{
  if(!stmt)
    return;

  for(const auto* decl : stmt->decls())
  {
    DeclVisitor::Visit(decl);
  }
}

void FunctionVisitor::VisitDefaultStmt(const DefaultStmt* stmt)
{
  _builder.buildSwitchDefaultStmt();
  StmtVisitor::Visit(stmt->getSubStmt());
}

void FunctionVisitor::VisitDoStmt(const DoStmt* stmt)
{
  _builder.buildDoStmt(makeExprBuilder(stmt->getCond()),
                       makeBlockBuilder(*this, stmt->getBody()));
}

void FunctionVisitor::VisitForStmt(const ForStmt* stmt)
{
  auto* initDeclStmt = dyn_cast_or_null<DeclStmt>(stmt->getInit());
  auto* condDeclStmt = stmt->getConditionVariableDeclStmt();

  auto build = [stmt, this] (FunctionBuilder& builder)
  {
    auto init = [stmt, this] (FunctionBuilder& builder)
    {
      if(!stmt->getConditionVariableDeclStmt())
      {
        StmtVisitor::Visit(stmt->getInit());
      }
    };
    auto cond = [stmt] (StmtBuilder& builder)
    {
      makeExprBuilder(stmt->getCond())(builder);
    };
    builder.buildForStmt(stmt->getCond() != nullptr,
                         init, cond, makeExprBuilder(stmt->getInc()),
                         makeBlockBuilder(*this, stmt->getBody()));
  };

  // If the condition declares a new variables we have to drag both the
  // initialier *and* the condition variable declaration out of the loop to
  // ensure the condition variable is declared *after* the initializer has run.
  if(condDeclStmt)
  {
    _builder.pushScope([this, initDeclStmt, condDeclStmt, build] (FunctionBuilder& builder)
    {
      VisitDeclStmt(initDeclStmt);
      VisitDeclStmt(condDeclStmt);
      build(builder);
    });
  }
  else
  {
    build(_builder);
  }
}

void FunctionVisitor::VisitIfStmt(const IfStmt* stmt)
{
  auto build = [stmt, this] ()
  {
    if(stmt->getElse())
    {
      _builder.buildIfStmt([stmt] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(stmt->getCond()); },
                           makeBlockBuilder(*this, stmt->getThen()),
                          makeBlockBuilder(*this, stmt->getElse()));
    }
    else
    {
      _builder.buildIfStmt([stmt] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(stmt->getCond()); },
                           makeBlockBuilder(*this, stmt->getThen()));
    }
  };

  buildStmtWithPossibleConditionVariable(stmt->getConditionVariable(),
                                         stmt->getConditionVariableDeclStmt(),
                                         build);
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

void FunctionVisitor::VisitSwitchStmt(const SwitchStmt* stmt)
{
  auto build = [stmt, this] ()
  {
    _builder.buildSwitchStmt([stmt] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(stmt->getCond()); },
                             makeBlockBuilder(*this, stmt->getBody()));
  };

  buildStmtWithPossibleConditionVariable(stmt->getConditionVariable(),
                                         stmt->getConditionVariableDeclStmt(),
                                         build);
}

void FunctionVisitor::VisitWhileStmt(const WhileStmt* stmt)
{
  auto build = [stmt, this] ()
  {
    _builder.buildWhileStmt([stmt] (StmtBuilder& builder) { ExprVisitor{builder}.Visit(stmt->getCond()); },
                            makeBlockBuilder(*this, stmt->getBody()));
  };

  buildStmtWithPossibleConditionVariable(stmt->getConditionVariable(),
                                         stmt->getConditionVariableDeclStmt(),
                                         build);
}

template<class F>
void FunctionVisitor::buildStmtWithPossibleConditionVariable(VarDecl* conditionVariable,
                                                             const DeclStmt* conditionVariableDeclStmt,
                                                             F build)
{
  if(conditionVariable)
  {
    // If the condition declares a variable open a new scope to ensure its name
    // doesn't clash with a variable that is already declared and makes it go
    // out of scope immediately following the while stmt.
    _builder.pushScope([&] (FunctionBuilder& builder)
                       {
                         VisitDeclStmt(conditionVariableDeclStmt);
                         build();
                       });
    // TODO: run destructors
  }
  else
  {
    build();
  }
}


void FunctionVisitor::VisitVarDecl(const VarDecl* decl)
{
  if(!decl->isStaticLocal())
  {
    if(const auto* init = decl->getInit())
    {
      _builder.declareVar(*decl, makeExprBuilder(init));
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
  _builder.buildStmt(makeExprBuilder(expr));
}

////////////////////////////////////////////////////////////////////////////////
// ExprVisitor
//

void ExprVisitor::VisitBinaryOperator(const BinaryOperator* expr)
{
  _builder.emitBinaryOperator(*expr, makeExprBuilder(expr->getLHS()), makeExprBuilder(expr->getRHS()));
}

void ExprVisitor::VisitCastExpr(const CastExpr* expr)
{
  // Even though the backend has to act according to the type of cast we can
  // either prefilter some of the stuff that should never reach codegen or
  // alter the process if necessary.
  switch(expr->getCastKind())
  {
    case CK_NoOp:
      Visit(expr->getSubExpr());
      break;

    case CK_LValueToRValue:
      _builder.emitCast(*expr, makeExprBuilder(expr->getSubExpr()));
      break;

    case CK_Dependent:
    case CK_BitCast:
    case CK_LValueBitCast:
    case CK_BaseToDerived:
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
    case CK_Dynamic:
    case CK_ToUnion:
    case CK_ArrayToPointerDecay:
    case CK_FunctionToPointerDecay:
    case CK_NullToPointer:
    case CK_NullToMemberPointer:
    case CK_BaseToDerivedMemberPointer:
    case CK_DerivedToBaseMemberPointer:
    case CK_MemberPointerToBoolean:
    case CK_ReinterpretMemberPointer:
    case CK_UserDefinedConversion:
    case CK_ConstructorConversion:
    case CK_IntegralToPointer:
    case CK_PointerToIntegral:
    case CK_PointerToBoolean:
    case CK_ToVoid:
    case CK_VectorSplat:
    case CK_IntegralCast:
    case CK_IntegralToBoolean:
    case CK_IntegralToFloating:
    case CK_FloatingToIntegral:
    case CK_FloatingToBoolean:
    case CK_FloatingCast:
    case CK_CPointerToObjCPointerCast:
    case CK_BlockPointerToObjCPointerCast:
    case CK_AnyPointerToBlockPointerCast:
    case CK_ObjCObjectLValueCast:
    case CK_FloatingRealToComplex:
    case CK_FloatingComplexToReal:
    case CK_FloatingComplexToBoolean:
    case CK_FloatingComplexCast:
    case CK_FloatingComplexToIntegralComplex:
    case CK_IntegralRealToComplex:
    case CK_IntegralComplexToReal:
    case CK_IntegralComplexToBoolean:
    case CK_IntegralComplexCast:
    case CK_IntegralComplexToFloatingComplex:
    case CK_ARCProduceObject:
    case CK_ARCConsumeObject:
    case CK_ARCReclaimReturnedObject:
    case CK_ARCExtendBlockObject:
    case CK_AtomicToNonAtomic:
    case CK_NonAtomicToAtomic:
    case CK_CopyAndAutoreleaseBlockObject:
    case CK_BuiltinFnToFnPtr:
    case CK_ZeroToOCLEvent:
    case CK_AddressSpaceConversion:
        llvm_unreachable("cast not supported");
  }
}

void ExprVisitor::VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr* expr)
{
  _builder.emitBooleanLiteral(*expr);
}

void ExprVisitor::VisitDeclRefExpr(const DeclRefExpr* expr)
{
  if(auto* var = dyn_cast<VarDecl>(expr->getDecl()))
  {
    _builder.emitVariableAccess(*var);
  }
  else
    llvm_unreachable("invalid decl ref target");
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
  _builder.emitParenExpr(makeExprBuilder(expr->getSubExpr()));
}

void ExprVisitor::VisitUnaryOperator(const UnaryOperator* expr)
{
  _builder.emitUnaryOperator(*expr, makeExprBuilder(expr->getSubExpr()));
}

////////////////////////////////////////////////////////////////////////////////
// buildModule
//

// TODO: Validate types against backend capabilities before sending stuff to print
// TODO: Backend-agnostic optimizations

namespace
{
  template<class EmittedEntities, class F>
  bool for_each_entity(ShaderStage stages, EmittedEntities& entities, F f)
  {
    for(auto& entity : entities)
    {
      if((entity.stages & stages) != ShaderStage::none)
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

  void buildFunction(ASTContext& ast, FunctionDecl& decl, FunctionBuilder& builder)
  {
    builder.setReturnType(decl.getReturnType());
    // We feed the arguments individually in case we have to transform them
    for(auto* param : decl.params())
    {
      builder.addParam(*param);
    }
    FunctionVisitor{ast, builder}.StmtVisitor::Visit(decl.getBody());
  }

  void buildFunctions(ASTContext& ast, ShaderContext& context, ModuleBuilder& module, ShaderStage stages)
  {
    for_each_entity(stages, context.functions, [&] (EmittedFunction& f)
    {
      return module.buildFunction(*f.decl, [&f, &ast] (FunctionBuilder& builder)
      {
        buildFunction(ast, *f.decl, builder);
      });
    });
  }
}

bool clang::lava::buildModule(ASTContext& ast, ShaderContext& context, ModuleBuilder& module, ShaderStage stages)
{
  buildRecords(context, module, stages);
  buildFunctions(ast, context, module, stages);

  return true;
}
