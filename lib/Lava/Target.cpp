//
//  Target.cpp
//  LLVM
//
//  Created by knejp on 7.10.15.
//
//

#include "clang/Lava/Target.h"

using namespace clang;
using namespace lava;

template class llvm::Registry<TargetPlugin, TargetRegistryTraits>;

Target::~Target() = default;

const TargetPlugin* lava::findTargetNamed(llvm::StringRef name)
{
  auto it = std::find_if(lava::TargetRegistry::begin(),
                         lava::TargetRegistry::end(),
                         [name] (const lava::TargetPlugin& target)
                         {
                           return target.name() == name;
                         });
  return it == lava::TargetRegistry::end() ? nullptr : &*it;
}
