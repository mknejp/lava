//
//  Target.h
//  LLVM
//
//  Created by knejp on 7.10.15.
//
//

#ifndef LLVM_CLANG_LAVA_TARGET_H
#define LLVM_CLANG_LAVA_TARGET_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Registry.h"

namespace clang
{
  class ASTContext;
  
  namespace lava
  {
    class Target;
    class TargetPlugin;
    struct TargetCapabilities;
    class ModuleBuilder;
    struct CodeGenOptions;

    struct TargetRegistryTraits
    {
      TargetRegistryTraits() = delete;

      using entry = TargetPlugin;
    };
    using TargetRegistry = llvm::Registry<TargetPlugin, TargetRegistryTraits>;

    template<class T>
    class TargetRegistryEntry;

    const TargetPlugin* findTargetNamed(llvm::StringRef name);

    template<class F>
    F forEachRegisteredTarget(F f);
  }
}

namespace llvm
{
  extern template class Registry<clang::lava::TargetPlugin, clang::lava::TargetRegistryTraits>;
  namespace cl
  {
    class OptionCategory;
  }
}

struct clang::lava::TargetCapabilities
{
  bool supportsMultistageModules : 1;
};

class clang::lava::Target
{
public:
  virtual ~Target();

  virtual TargetCapabilities capabilities() const = 0;
  virtual ModuleBuilder makeModuleBuilder(ASTContext& ast, CodeGenOptions cgOpts) const = 0;

protected:
  Target() = default;
  Target(const Target&) = default;
  Target(Target&&) = default;
  Target& operator=(const Target&) = default;
  Target& operator=(Target&&) = default;
};

class clang::lava::TargetPlugin
{
public:
  llvm::StringRef name() const { return _name; }
  llvm::StringRef desc() const { return _desc; }
  llvm::StringRef extension() const { return _extension; }
  virtual auto registerCommandLineArguments() const -> llvm::ArrayRef<llvm::cl::OptionCategory*> = 0;
  virtual auto targetFromCommandLineArguments() const -> std::unique_ptr<Target> = 0;

protected:
  TargetPlugin(llvm::StringRef name, llvm::StringRef desc, llvm::StringRef extension)
  : _name(name), _desc(desc), _extension(extension) { }

  ~TargetPlugin() = default;
  TargetPlugin(const TargetPlugin&) = default;
  TargetPlugin(TargetPlugin&&) = default;
  TargetPlugin& operator=(const TargetPlugin&) = default;
  TargetPlugin& operator=(TargetPlugin&&) = default;

  llvm::StringRef _name;
  llvm::StringRef _desc;
  llvm::StringRef _extension;
};

template<class T>
class clang::lava::TargetRegistryEntry
{
public:
  template<class... Args>
  TargetRegistryEntry(Args&&... args) : _target(std::forward<Args>(args)...), _node(_target) {}

private:
  const T _target;
  TargetRegistry::node _node;
};

template<class F>
F clang::lava::forEachRegisteredTarget(F f)
{
  return std::for_each(TargetRegistry::begin(), TargetRegistry::end(), std::move(f));
}

#endif // LLVM_CLANG_LAVA_TARGET_H
