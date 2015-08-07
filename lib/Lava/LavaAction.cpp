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
#include "clang/Lava/ModuleBuilder.h"
#include "clang/Lava/GLSL.h"
#include "clang/Lava/SPIRV.h"
#include "clang/Sema/SemaConsumer.h"
#include "SPIRV/disassemble.h"
#include "SPIRV/doc.h"
#include "SPIRV/GLSL450Lib.h"
#include <sstream>
const char* GlslStd450DebugNames[GLSL_STD_450::Count];

using namespace clang;
using namespace lava;

LangOptions lava::DefaultLangOptions()
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
  Consumer(CompilerInstance &CI, StringRef file, ShaderContext& ctx)
  : _ci(CI)
  , _file(file)
  , _ctx(ctx)
  { }

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
    _ctx.functions.erase(std::remove_if(_ctx.functions.begin(), _ctx.functions.end(), [this] (const EmittedFunction& f)
                                        {
                                          return f.decl == _ctx.vertexFunction || f.decl == _ctx.fragmentFunction;
                                        }),
                         _ctx.functions.end());

    for(const auto& func : _ctx.functions)
    {
      func.decl->getNameForDiagnostic(llvm::errs(), _ci.getASTContext().getPrintingPolicy(), true);
      llvm::errs() << '\n';
      func.decl->dump();
    }

    _ctx.vertexFunction->getNameForDiagnostic(llvm::errs(), _ci.getASTContext().getPrintingPolicy(), true);
    llvm::errs() << '\n';
    _ctx.vertexFunction->dump();
    {
      // GLSL
      auto module = glsl::createModuleBuilder(_ci.getASTContext());
      buildModule(_ctx, module, ShaderStage::vertex);
      llvm::errs() << module.moduleContent();
    }
    llvm::errs() << "====================\n";
    {
      // SPIR-V
      spv::Parameterize();
      GLSL_STD_450::GetDebugNames(GlslStd450DebugNames);
      auto module = spirv::createModuleBuilder(_ci.getASTContext());
      buildModule(_ctx, module, ShaderStage::vertex);
      printSpirv(module.moduleContent());
    }

    printEntry("vert", _ctx.vertexFunction);
    printEntry("frag", _ctx.fragmentFunction);
  }

  void printSpirv(const std::string& string)
  {
    auto spirv = std::vector<unsigned>{};
    spirv.resize(string.size() / sizeof(unsigned));
    std::memcpy(&spirv[0], string.data(), string.size());
    printSpirv(spirv);
  }

  void printSpirv(const std::vector<unsigned>& spirv)
  {
    std::ostringstream disassembly;
    spv::Disassemble(disassembly, spirv);
    llvm::errs() << disassembly.str();
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

  CompilerInstance& _ci;
  StringRef _file;
  ShaderContext& _ctx;
};


Action::Action(ShaderContext& shaderContext)
: _shaderContext(shaderContext)
{
}

bool Action::BeginInvocation(CompilerInstance &CI)
{
  CI.getLangOpts() = DefaultLangOptions();
  return super::BeginInvocation(CI);
}

std::unique_ptr<ASTConsumer>
Action::CreateASTConsumer(CompilerInstance &CI, StringRef file)
{
  return llvm::make_unique<Consumer>(CI, file, _shaderContext);
}
