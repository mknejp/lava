//
//  Target_SPIRV.cpp
//  LLVM
//
//  Created by knejp on 20.10.15.
//
//

#include "clang/Lava/SPIRV.h"
#include "clang/Lava/LavaAction.h"
#include "clang/Lava/ModuleBuilder.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace lava;
using llvm::StringRef;

namespace
{
  StringRef versionName(spirv::Version v)
  {
    switch(v)
    {
      case spirv::Version::spv_100: return "spv-100";
    }
  }

  StringRef versionDesc(spirv::Version v)
  {
    switch(v)
    {
      case spirv::Version::spv_100: return "SPIR-V 0.99 (revision 31)";
    }
  }

  namespace config
  {
    bool disassembleKHR = false;
  }

  // This function is shared by all SPIR-V targets as they all have the same
  // command line options. static makes sure they are only registered once.
  llvm::ArrayRef<llvm::cl::OptionCategory*> registerCommonCommandLineArguments()
  {
    namespace cl = llvm::cl;
    static cl::OptionCategory category("SPIR-V Options");
    static cl::opt<bool, true> disassembleKHR("spv-disassemble-khr",
                                              cl::desc("Use the Khronos human-readable representation of SPIR-V bytecode"),
                                              cl::Optional,
                                              cl::location(config::disassembleKHR),
                                              cl::cat(category));
    return llvm::makeArrayRef(&category);
  }
}

spirv::Target::Target(Version version, Options opts)
: _version(version)
, _opts(std::move(opts))
{
}

TargetCapabilities spirv::Target::capabilities() const
{
  TargetCapabilities caps;
  caps.supportsMultistageModules = true;
  return caps;
}

ModuleBuilder spirv::Target::makeModuleBuilder(ASTContext& ast, CodeGenOptions cgOpts) const
{
  return spirv::createModuleBuilder(ast, _opts);
}

spirv::Plugin::Plugin(Version v)
: lava::TargetPlugin(versionName(v), versionDesc(v), "spv")
, _version(v)
{
}

llvm::ArrayRef<llvm::cl::OptionCategory*> spirv::Plugin::registerCommandLineArguments() const
{
  return ::registerCommonCommandLineArguments();
}

std::unique_ptr<Target> spirv::Plugin::targetFromCommandLineArguments() const
{
  Options opts;
  opts.disassembleKHR = config::disassembleKHR;
  return llvm::make_unique<spirv::Target>(_version, opts);
}
