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
#include "clang/AST/Mangle.h"
#include "clang/Lava/IndentWriter.h"
#include "clang/lava/GLSL.h"

using namespace clang;
using namespace lava;

ModuleBuilder::Concept::~Concept() = default;
void RecordBuilder::vftbl() { }
void FunctionBuilder::vftbl() { }

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

  bool buildFunction(FunctionDecl& decl, FunctionBuilder& builder)
  {
    builder.setReturnType(decl.getReturnType());
    // We feed the arguments individually in case we have to transform them
    for(auto* param : decl.params())
    {
      builder.addParam(param);
    }
    builder.pushScope([&] (FunctionBuilder& builder) { });
    return true;
  }

  bool buildFunctions(ShaderContext& context, ModuleBuilder& module, ShaderStage stage)
  {
    return for_each_entity(stage, context.functions, [&] (EmittedFunction& f)
    {
      return module.buildFunction(*f.decl, [&f] (FunctionBuilder& builder)
      {
        buildFunction(*f.decl, builder);
      });
      return false;
    });
  }
}

bool clang::lava::buildModule(ShaderContext& context, ModuleBuilder& module, ShaderStage stage)
{
  buildRecords(context, module, stage);
  buildFunctions(context, module, stage);

  return true;
}
