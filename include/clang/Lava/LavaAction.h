//===--- LavaFrontendAction.h - Lava information gathering ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the LavaFrontendAction class, which is used to collect
// information about declarations in graphics shader stages.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LAVA_LAVAFRONTENDACTION_H
#define LLVM_CLANG_LAVA_LAVAFRONTENDACTION_H

#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lava/ShaderContext.h"

namespace clang
{

namespace lava
{
  class Action;
  class Consumer;
  class EntryPointVisitor;
  class GatheringVisitor;
  class TestAction;

  struct ShaderContext;

  LangOptions DefaultLangOptions();

} // end namespace lava
} // end namespace clang

class clang::lava::Action : public ASTFrontendAction
{
  using super = ASTFrontendAction;
  
public:
  Action(ShaderContext& shaderContext);

  bool BeginInvocation(CompilerInstance &CI) override;
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override;

private:
  ShaderContext& _shaderContext;
};

class clang::lava::TestAction : public Action
{
public:
  TestAction() : Action(_shaderContext) { }

private:
  ShaderContext _shaderContext;
};

#endif // LLVM_CLANG_LAVA_LAVAFRONTENDACTION_H
