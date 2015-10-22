//===--- GLSL.h - ModuleBuilder factory for GLSL ----------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_GLSL_H
#define LLVM_CLANG_LAVA_GLSL_H

#include "clang/lava/Target.h"

namespace clang
{
  class ASTContext;

  namespace lava
  {
    class ModuleBuilder;

    namespace glsl
    {
      ModuleBuilder createModuleBuilder(ASTContext& ast);

      enum class Version;

      struct Options;
      class Plugin;
      class Target;
    }
  }
}

enum class clang::lava::glsl::Version
{
  glsl_300_es,
};

struct clang::lava::glsl::Options
{
  // TODO
};

class clang::lava::glsl::Target : public lava::Target
{
public:
  Target(Version version, Options opts);

  TargetCapabilities capabilities() const override;
  ModuleBuilder makeModuleBuilder(ASTContext& ast, CodeGenOptions cgOpts) const override;

private:
  Version _version;
  Options _opts;
};

class clang::lava::glsl::Plugin final : public TargetPlugin
{
public:
  Plugin(Version version);

  auto registerCommandLineArguments() const -> llvm::ArrayRef<llvm::cl::OptionCategory*> override;
  auto targetFromCommandLineArguments() const -> std::unique_ptr<lava::Target> override;

private:
  Version _version;
};

#endif // LLVM_CLANG_LAVA_GLSL_H
