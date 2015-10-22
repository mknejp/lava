//===--- LavaAction.cpp - Lava frontend actions -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Lava/LavaAction.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lava/CodeGen.h"
#include "clang/Lava/ModuleBuilder.h"
#include "clang/Lava/GLSL.h"
#include "clang/Lava/SPIRV.h"
#include "clang/Sema/SemaConsumer.h"
#include <sstream>

using namespace clang;
using namespace lava;
using llvm::StringRef;

namespace
{
  struct TranslatedModules
  {
    CodeGenModule merged; // If this is non-empty the others are ignored
    CodeGenModule frag;
    CodeGenModule vert;
  };
}

////////////////////////////////////////////////////////////////////////////////
// EntryPointVisitor
//

class lava::EntryPointVisitor : public RecursiveASTVisitor<EntryPointVisitor>
{
public:
  EntryPointVisitor(ShaderContext& ctx) : _ctx(ctx) { }

  bool VisitFunctionDecl(FunctionDecl* f)
  {
    if(f->hasAttr<LavaFragmentAttr>())
    {
      return assignEntryFunction(f, _ctx.fragmentFunction);
    }
    else if(f->hasAttr<LavaVertexAttr>())
    {
      return assignEntryFunction(f, _ctx.vertexFunction);
    }
    else
    {
      return true;
    }
  }

private:
  bool assignEntryFunction(FunctionDecl* f, FunctionDecl*& target)
  {
    if(target)
    {
      // TODO: duplicate, emit diagnostic
      return false;
    }
    else
    {
      target = f;
      return true;
    }
  }

  ShaderContext& _ctx;
};

////////////////////////////////////////////////////////////////////////////////
// GatheringVisitor
//

// This visitor gathers all types, variables and functions reachable from entry points.
class lava::GatheringVisitor : public RecursiveASTVisitor<GatheringVisitor>
{
public:
  GatheringVisitor(ASTContext& ast, DiagnosticsEngine& diag)
  : _ast(ast)
  , _diag(diag)
  {
  }

  bool VisitEntryPoints(std::initializer_list<std::pair<FunctionDecl*, ShaderStage>> entryPoints);

  bool VisitCXXConstructExpr(CXXConstructExpr* expr);
  bool VisitCXXRecordDecl(CXXRecordDecl* decl);
  bool VisitDeclRefExpr(DeclRefExpr* dre);
  bool VisitFunctionDecl(FunctionDecl* decl);
  bool VisitRecordType(RecordType* type);

  bool shouldVisitImplicitCode() const { return true; }

  void setShaderStage(ShaderStage stage)
  {
    _stage = stage;
  }

  std::vector<EmittedRecord> emittedRecords() const;
  std::vector<EmittedFunction> emittedFunctions() const;

private:
  ASTContext& _ast;
  DiagnosticsEngine& _diag;
  ShaderStage _stage = ShaderStage::none;

  // The order in the vector matters.
  // While traversing the AST, records other records transitively depend on
  // (base classes and fields) are appended to the end. When done with the AST
  // all duplicates are removed and the order reversed to represent the order at
  // which they must be emitted in a text-based code generator to guarantee all
  // dependencies are defined before use. Since records in Lava cannot contain
  // references or pointers no forward declarations are required.
  std::vector<EmittedRecord> _records;

  std::vector<EmittedFunction> _functions;
};

bool GatheringVisitor::VisitEntryPoints(std::initializer_list<std::pair<FunctionDecl*, ShaderStage>> entryPoints)
{
  for(const auto& entry : entryPoints)
  {
    setShaderStage(entry.second);
    if(!TraverseDecl(entry.first))
    {
      return false;
    }
  }
  return true;
}

bool GatheringVisitor::VisitCXXConstructExpr(CXXConstructExpr* expr)
{
  return TraverseDecl(expr->getConstructor());
}

bool GatheringVisitor::VisitCXXRecordDecl(CXXRecordDecl* decl)
{
  _records.push_back({decl, _stage});
  for(const CXXBaseSpecifier& base : decl->bases())
  {
    if(auto* record = base.getType()->getAsCXXRecordDecl())
    {
      VisitCXXRecordDecl(record);
    }
  }
  auto handleField = [this] (const ValueDecl& var)
  {
    if(auto* arr = var.getType()->getAsArrayTypeUnsafe())
    {
      if(auto* record = arr->getElementType()->getAsCXXRecordDecl())
      {
        VisitCXXRecordDecl(record);
      }
    }
    else if(auto* record = var.getType()->getAsCXXRecordDecl())
    {
      VisitCXXRecordDecl(record);
    }
  };
  if(!decl->isLambda())
  {
    for(const FieldDecl* field : decl->fields())
    {
      handleField(*field);
    }
  }
  else
  {
    for(const LambdaCapture& capture : decl->captures())
    {
      assert(!capture.capturesThis() && "'this' captures not supported");
      handleField(*capture.getCapturedVar());
    }
  }
  return true;
}

bool GatheringVisitor::VisitDeclRefExpr(DeclRefExpr* dre)
{
  if(auto* record = dre->getType()->getAsCXXRecordDecl())
  {
    VisitCXXRecordDecl(record);
  }
  else if(auto* f = dre->getDecl()->getAsFunction())
  {
    TraverseDecl(f);
  }
  return true;
}

bool GatheringVisitor::VisitFunctionDecl(FunctionDecl* decl)
{
  if(!decl->doesThisDeclarationHaveABody())
    return true;

  _functions.push_back({decl, _stage});
  return true;
}

bool GatheringVisitor::VisitRecordType(RecordType* type)
{
  return VisitCXXRecordDecl(type->getAsCXXRecordDecl());
}

std::vector<EmittedRecord> GatheringVisitor::emittedRecords() const
{
  auto finalRecords = std::vector<EmittedRecord>{};
  finalRecords.reserve(_records.size());

  // To avoid duplicates remember the already emitted records.
  // Since the new vector cannot have more members than the old no reallocation
  // will happen due to the reserve() so we store pointers directly into the
  // already stored EmittedRecord objects.
  auto savedRecords = llvm::DenseMap<const CXXRecordDecl*, EmittedRecord*>{};

  // Traverse the records from back to front and only keep the first occurence
  // of each record. This ensures the final order is a linearized dependency
  // graph of all records.
  for(auto it = _records.crbegin(), end = _records.crend(); it != end; ++it)
  {
    auto saved = savedRecords.find(it->decl);
    if(saved != savedRecords.end())
    {
      // The records is already present, so we only need to add the stage this
      // is used in.
      saved->second->stages |= it->stages;
    }
    else
    {
      finalRecords.push_back(*it);
      savedRecords[it->decl] = &finalRecords.back();
    }
  }
  return finalRecords;
}

std::vector<EmittedFunction> GatheringVisitor::emittedFunctions() const
{
  auto finalFuncs = std::vector<EmittedFunction>{};

  // A function can be present multiple times if it is discovered in different
  // shader stages. Collapse all those definitions into one and merge the stages.
  auto savedFuncs = llvm::DenseMap<const FunctionDecl*, EmittedFunction*>{};
  for(auto it = _functions.crbegin(), end = _functions.crend(); it != end; ++it)
  {
    auto saved = savedFuncs.find(it->decl);
    if(saved != savedFuncs.end())
    {
      // The records is already present, so we only need to add the stage this
      // is used in.
      saved->second->stages |= it->stages;
    }
    else
    {
      finalFuncs.push_back(*it);
      savedFuncs[it->decl] = &finalFuncs.back();
    }
  }
  return finalFuncs;
}

////////////////////////////////////////////////////////////////////////////////
// Consumer
//

class lava::Consumer : public SemaConsumer
{
  using super = SemaConsumer;

public:
  Consumer(CompilerInstance &CI,
           StringRef file,
           TargetCapabilities capabilities,
           std::function<ModuleBuilder()> moduleBuilderFactory)
  : _ci(CI)
  , _file(file)
  , _capabilities(std::move(capabilities))
  , _moduleBuilderFactory(std::move(moduleBuilderFactory))
  { }

  const TranslatedModules& modules() const { return _modules; }

private:
  void HandleTranslationUnit(ASTContext& Context) override
  {
    if(!EntryPointVisitor(_ctx).TraverseDecl(Context.getTranslationUnitDecl()))
      return;

    GatheringVisitor gather{Context, _ci.getDiagnostics()};
    gather.VisitEntryPoints(
    {
      {_ctx.vertexFunction, ShaderStage::vertex},
      {_ctx.fragmentFunction, ShaderStage::fragment},
    });
    _ctx.records = gather.emittedRecords();
    _ctx.functions = gather.emittedFunctions();

    // Remove entry points, they need special generation
//    _ctx.functions.erase(std::remove_if(_ctx.functions.begin(), _ctx.functions.end(), [this] (const EmittedFunction& f)
//                                        {
//                                          return f.decl == _ctx.vertexFunction || f.decl == _ctx.fragmentFunction;
//                                        }),
//                         _ctx.functions.end());

//    for(const auto& func : _ctx.functions)
//    {
//      func.decl->getNameForDiagnostic(llvm::errs(), _ci.getASTContext().getPrintingPolicy(), true);
//      llvm::errs() << '\n';
//      func.decl->dump();
//    }


    auto generateModule = [this] (ShaderStage stage)
    {
      auto moduleBuilder = _moduleBuilderFactory();
      if(buildModule(_ci.getASTContext(), _ctx, moduleBuilder, stage))
        return moduleBuilder.reset();
      return CodeGenModule{};
    };

    auto maybeGenerateModule = [generateModule] (FunctionDecl* entryPoint, ShaderStage stage)
    {
      return entryPoint ? generateModule(stage) : CodeGenModule{};
    };

    if(_capabilities.supportsMultistageModules)
    {
      _modules.merged = generateModule(ShaderStage::all);
    }
    else
    {
      _modules.frag = maybeGenerateModule(_ctx.fragmentFunction, ShaderStage::fragment);
      _modules.vert = maybeGenerateModule(_ctx.vertexFunction, ShaderStage::vertex);
    }
  }

  void printEntry(llvm::StringRef name, NamedDecl* decl)
  {
    llvm::errs() << name << " main";
    if(decl)
    {
      print("", decl);
    }
    else
    {
      llvm::errs() << ": n/a\n";
    }
  }
  void print(llvm::StringRef mangled, NamedDecl* decl)
  {
    llvm::errs() << mangled << ": ";
    decl->getNameForDiagnostic(llvm::errs(), PrintingPolicy(_ci.getLangOpts()), true);
    llvm::errs() << '\n';
  }

  ShaderContext _ctx;
  TranslatedModules _modules;
  CompilerInstance& _ci;
  StringRef _file;
  TargetCapabilities _capabilities;
  std::function<ModuleBuilder()> _moduleBuilderFactory;
};

////////////////////////////////////////////////////////////////////////////////
// CodeGenAction
//

CodeGenAction::CodeGenAction(const Target& target, OutputOptions outOpts, lava::CodeGenOptions cgOpts)
: _target(target)
, _outOpts(std::move(outOpts))
, _cgOpts(std::move(cgOpts))
{
}

std::unique_ptr<ASTConsumer>
CodeGenAction::CreateASTConsumer(CompilerInstance &CI, StringRef file)
{
  // Normally this is where all other FrontendActions generate their
  // file streams but we don't know how many files we need, so we have to
  // delegate this to EndSoureFileAction.
  return llvm::make_unique<Consumer>(CI, file, _target.capabilities(), [this]
  {
    return _target.makeModuleBuilder(getCompilerInstance().getASTContext(), {});
  });
}

void CodeGenAction::EndSourceFileAction()
{
  auto& ci = getCompilerInstance();
  if(!ci.hasASTConsumer())
    return;
  if(ci.getDiagnostics().hasErrorOccurred())
    return;

  auto& modules = static_cast<Consumer&>(getCompilerInstance().getASTConsumer()).modules();

  SmallString<128> out{ci.getFrontendOpts().OutputFile.begin(), ci.getFrontendOpts().OutputFile.end()};

  std::function<void(StringRef ext, const CodeGenModule&)> streamer;
  if(out != "-")
  {
    llvm::sys::path::append(out, llvm::sys::path::filename(getCurrentFile()));
    llvm::sys::path::replace_extension(out, "");

    streamer = [&out, &ci] (StringRef ext, const CodeGenModule& module)
    {
      auto file = out;
      llvm::sys::path::replace_extension(file, ext);
      if(auto* stream = ci.createOutputFile(file, true, true, "", "", true, true))
      {
        (*stream) << module;
      }
    };
  }
  else if(auto* stream = ci.createDefaultOutputFile(true, "", ""))
  {
    streamer = [stream] (StringRef, const CodeGenModule& module)
    {
      (*stream) << module;
    };
  }
  if(streamer)
  {
    if(modules.merged)
    {
      streamer(_outOpts.fileExt, modules.merged);
    }
    else
    {
      if(modules.vert)
      {
        streamer(_outOpts.fileExtVertex, modules.vert);
      }
      if(modules.frag)
      {
        streamer(_outOpts.fileExtFragment, modules.frag);
      }
    }
  }
}
