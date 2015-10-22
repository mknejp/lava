//===--- SPIRV.h - ModuleBuilder factory for SPIR-V -------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_LAVA_SPIRV_H
#define LLVM_CLANG_LAVA_SPIRV_H

#include "clang/lava/Target.h"

namespace clang
{
  class ASTContext;

  namespace lava
  {
    class ModuleBuilder;

    namespace spirv
    {
      enum class Version;

      struct Options;
      class Plugin;
      class Target;

      ModuleBuilder createModuleBuilder(ASTContext& ast, Options opts);
    }
  }
}

enum class clang::lava::spirv::Version
{
  spv_100,
};

struct clang::lava::spirv::Options
{
  /// Use the Khronos human-readable representation of SPIR-V bytecode
  bool disassembleKHR : 1;
};

class clang::lava::spirv::Target : public lava::Target
{
public:
  Target(Version version, Options opts);

  TargetCapabilities capabilities() const override;
  ModuleBuilder makeModuleBuilder(ASTContext& ast, CodeGenOptions cgOpts) const override;

private:
  Version _version;
  Options _opts;
};

class clang::lava::spirv::Plugin final : public TargetPlugin
{
public:
  Plugin(Version version);

  auto registerCommandLineArguments() const -> llvm::ArrayRef<llvm::cl::OptionCategory*> override;
  auto targetFromCommandLineArguments() const -> std::unique_ptr<lava::Target> override;

private:
  Version _version;
};

#endif // LLVM_CLANG_LAVA_SPIRV_H
