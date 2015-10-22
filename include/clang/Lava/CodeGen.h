//
//  CodeGen.h
//  LLVM
//
//  Created by knejp on 17.10.15.
//
//

#ifndef LLVM_CLANG_LAVA_CODEGEN_H
#define LLVM_CLANG_LAVA_CODEGEN_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/STLExtras.h"

namespace clang
{
  class LangOptions;
  class TargetOptions;
  
  namespace lava
  {
    struct CodeGenOptions;
    class CodeGenModule;

    LangOptions langOptions();
    TargetOptions targetOptions();
    
  }
}

struct clang::lava::CodeGenOptions
{
  // TODO
};

/// Holds the result of a fully codegen'd target module
class clang::lava::CodeGenModule
{
public:
  CodeGenModule() = default;

  template<class T>
  CodeGenModule(T&& module)
  : _module(llvm::make_unique<Model<typename std::decay<T>::type>>(std::forward<T>(module)))
  { }

  constexpr explicit operator bool() const { return bool(_module); }

  friend llvm::raw_ostream& operator<<(llvm::raw_ostream& out, const CodeGenModule& me)
  {
    if(me)
      me._module->serialize(out);
    return out;
  }

private:
  struct Concept
  {
    virtual ~Concept();
    virtual void serialize(llvm::raw_ostream& out) const = 0;
  };

  template<class T>
  struct Model : Concept
  {
    template<class... Args>
    Model(Args&&... args) : x(std::forward<Args>(args)...) { }
    void serialize(llvm::raw_ostream& out) const override { out << x; }
    T x;
  };

  std::unique_ptr<Concept> _module;
};



#endif // LLVM_CLANG_LAVA_CODEGEN_H
