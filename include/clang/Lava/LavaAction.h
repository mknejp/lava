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
#include "clang/Lava/CodeGen.h"

namespace clang
{

namespace lava
{
  class Consumer;
  class CodeGenAction;
  class EntryPointVisitor;
  class GatheringVisitor;
  struct OutputOptions;

  class Target;

} // end namespace lava
} // end namespace clang

struct clang::lava::OutputOptions
{
  std::string fileExt;
  std::string fileExtFragment;
  std::string fileExtVertex;
};

class clang::lava::CodeGenAction : public ASTFrontendAction
{
  using super = ASTFrontendAction;
  
public:
  CodeGenAction(const Target& target, OutputOptions outOpts, CodeGenOptions cgOpts);

  void EndSourceFileAction() override;
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override;

private:
  const Target& _target;
  const OutputOptions _outOpts;
  const CodeGenOptions _cgOpts;
};

#endif // LLVM_CLANG_LAVA_LAVAFRONTENDACTION_H
