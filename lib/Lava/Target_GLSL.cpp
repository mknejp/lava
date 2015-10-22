//
//  Target_GLSL.cpp
//  LLVM
//
//  Created by knejp on 16.10.15.
//
//

#include "clang/Lava/GLSL.h"
#include "clang/Lava/LavaAction.h"
#include "clang/Lava/ModuleBuilder.h"

using namespace clang;
using namespace lava;
using llvm::StringRef;

namespace
{
  StringRef versionName(glsl::Version v)
  {
    switch(v)
    {
      case glsl::Version::glsl_300_es: return "glsl-300-es";
    }
  }

  StringRef versionDesc(glsl::Version v)
  {
    switch(v)
    {
      case glsl::Version::glsl_300_es: return "OpenGL ES Shading Language 3.00.4";
    }
  }
}

glsl::Target::Target(Version version, Options opts)
{
}

TargetCapabilities glsl::Target::capabilities() const
{
  TargetCapabilities caps;
  caps.supportsMultistageModules = false;
  return caps;
}

ModuleBuilder glsl::Target::makeModuleBuilder(ASTContext& ast, CodeGenOptions cgOpts) const
{
  return glsl::createModuleBuilder(ast);
}

glsl::Plugin::Plugin(Version v)
: lava::TargetPlugin(versionName(v), versionDesc(v), "glsl")
, _version(v)
{
}

llvm::ArrayRef<llvm::cl::OptionCategory*> glsl::Plugin::registerCommandLineArguments() const
{
  return {};
}

std::unique_ptr<Target> glsl::Plugin::targetFromCommandLineArguments() const
{
  return llvm::make_unique<glsl::Target>(_version, Options{});
}
