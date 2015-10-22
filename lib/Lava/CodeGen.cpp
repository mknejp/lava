//
//  CodeGen.cpp
//  LLVM
//
//  Created by knejp on 17.10.15.
//
//

#include "clang/Lava/CodeGen.h"

#include "clang/Basic/LangOptions.h"

using namespace clang;
using namespace lava;

LangOptions lava::langOptions()
{
  LangOptions opts;
  opts.CPlusPlus = 1;
  opts.CPlusPlus11 = 1;
  opts.CPlusPlus14 = 1;
  opts.Lava = 1;
  opts.LineComment = 1;
  opts.Bool = 1;
  opts.Exceptions = 0;
  opts.RTTI = 0;
  opts.RTTIData = 0;
  return opts;
}

CodeGenModule::Concept::~Concept() = default;

