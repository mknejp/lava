//===------- Lava.cpp - Main file for Lava graphics shader compiler -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file implements the C++11 feature migration tool main function
/// and transformation framework.
///
/// See user documentation for usage instructions.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Format/Format.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Lava/CodeGen.h"
#include "clang/Lava/GLSL.h"
#include "clang/Lava/LavaAction.h"
#include "clang/Lava/ShaderContext.h"
#include "clang/Lava/SPIRV.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "Version.h"

#include <iostream>

namespace cl = llvm::cl;
using namespace clang;
using namespace clang::tooling;

namespace
{
  ////////////////////////////////////////////////////////////////////////////////
  // Builtin Targets

  auto glsl =
  {
    lava::TargetRegistryEntry<lava::glsl::Plugin>{lava::glsl::Version::glsl_300_es}
  };
  auto spirv =
  {
    lava::TargetRegistryEntry<lava::spirv::Plugin>{lava::spirv::Version::spv_100}
  };

  // All options must belong to locally defined categories for them to get shown
  // by -help. We explicitly hide everything else (except -help and -version).
  cl::OptionCategory GeneralCategory("General Options");
  cl::OptionCategory LanguageCategory("Language Options");
  cl::OptionCategory OutputCategory("Output Options");
  cl::OptionCategory CodeGenCategory("CodeGen Options");

  std::vector<cl::OptionCategory*> VisibleCategories
  {
    &GeneralCategory, &LanguageCategory, &OutputCategory, &CodeGenCategory
  };

//  static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
//  cl::extrahelp Help(
//      "EXAMPLES:\n\n"
//      "Apply all transforms on a file that doesn't require compilation arguments:\n\n"
//      "  clang-modernize file.cpp\n"
//      "\n"
//      "Convert for loops to ranged-based for loops for all files in the compilation\n"
//      "database that belong in a project subtree and then reformat the code\n"
//      "automatically using the LLVM style:\n\n"
//      "    clang-modernize -p build/path -include project/path -format -loop-convert\n"
//      "\n"
//      "Make use of both nullptr and the override specifier, using git ls-files:\n"
//      "\n"
//      "  git ls-files '*.cpp' | xargs -I{} clang-modernize -p build/path \\\n"
//      "      -use-nullptr -add-override -override-macros {}\n"
//      "\n"
//      "Apply all transforms supported by both clang >= 3.0 and gcc >= 4.7 to\n"
//      "foo.cpp and any included headers in bar:\n\n"
//      "  clang-modernize -for-compilers=clang-3.0,gcc-4.7 foo.cpp \\\n"
//      "      -include bar -- -std=c++11 -Ibar\n\n");
  cl::extrahelp CommonHelp("\n"
                           "Add extra help text here"
                           "\n"
                           );

  const auto Overview =
  "\n"
  "Add overview text here"
  "\n"
  ;

  // TODO: options with cl::ReallyHidden are not implemented yet and their names/descriptions may change

  //////////////////////////////////////////////////////////////////////////////
  // General Options

  cl::list<std::string> SourcePaths(cl::Positional,
                                    cl::desc("<inputs>"),
                                    cl::ZeroOrMore,
                                    cl::cat(GeneralCategory));

  //////////////////////////////////////////////////////////////////////////////
  // Language Options

  cl::list<std::string> HeaderSearchPaths("I",
                                          cl::desc("Add directory to include search paths"),
                                          cl::value_desc("dir"),
                                          cl::ZeroOrMore,
                                          cl::Prefix,
                                          cl::cat(LanguageCategory));
  cl::list<std::string> Macros("D",
                               cl::desc("Define a preprocessor token"),
                               cl::value_desc("name"),
                               cl::ZeroOrMore,
                               cl::Prefix,
                               cl::cat(LanguageCategory));

  //////////////////////////////////////////////////////////////////////////////
  // CodeGen Options

  cl::opt<const lava::TargetPlugin*> Target("target",
                                            cl::desc("Select the target to generate code for (required)"),
                                            cl::Required,
                                            cl::cat(CodeGenCategory));
  cl::opt<bool> NoStrip("no-strip",
                        cl::desc("Do not remove unused internal symbols"),
                        cl::init(false),
                        cl::Optional,
                        cl::ReallyHidden,
                        cl::cat(CodeGenCategory));
  cl::opt<bool> NoSource("no-source-info",
                         cl::desc("Do not keep source code referencing information"),
                         cl::init(false),
                         cl::Optional,
                         cl::ReallyHidden,
                         cl::cat(CodeGenCategory));
  cl::opt<bool> NoNames("no-names-info",
                        cl::desc("Remove or obfuscate names of internal symbols"),
                        cl::init(false),
                        cl::Optional,
                        cl::ReallyHidden,
                        cl::cat(CodeGenCategory));
  cl::opt<lava::ShaderStage> Stage("stage",
                                   cl::desc("Generate only a single shader stage"),
                                   cl::ReallyHidden,
                                   cl::Optional,
                                   cl::values(clEnumValN(lava::ShaderStage::vertex, "vert", "Vertex"),
                                              clEnumValN(lava::ShaderStage::fragment, "frag", "Fragment"),
                                              clEnumValEnd),
                                   cl::cat(CodeGenCategory));

  //////////////////////////////////////////////////////////////////////////////
  // Output Options

  cl::opt<std::string> Output("o",
                              cl::desc("Write output to this directory (required)"),
                              cl::value_desc("dir"),
                              cl::ValueRequired,
                              cl::Optional,
                              cl::init("."),
                              cl::cat(OutputCategory));

  cl::opt<std::string> ExtFragment("ext-frag",
                                   cl::desc("The extension to append to fragment shader output files (default 'frag')"),
                                   cl::init("frag"),
                                   cl::Optional,
                                   cl::cat(OutputCategory));
  cl::opt<std::string> ExtVertex("ext-vert",
                                 cl::desc("The extension to append to vertex shader output files (default 'vert')"),
                                 cl::init("vert"),
                                 cl::Optional,
                                 cl::cat(OutputCategory));
  cl::opt<std::string> Extension("ext",
                                 cl::desc("The extension to append to output files (default is derived from -target)"),
                                 cl::Optional,
                                 cl::cat(OutputCategory));
  cl::opt<bool> StripExt("strip-ext",
                         cl::desc("Strip the input file extension before appending the stage and target specific extensions"),
                         cl::init(false),
                         cl::ReallyHidden,
                         cl::cat(OutputCategory));
  cl::opt<bool> SplitStages("split-stages",
                            cl::desc("Split the result into one file per shader stage if the target builds multistage files by default"),
                            cl::init(false),
                            cl::ReallyHidden,
                            cl::cat(OutputCategory));
  
  //////////////////////////////////////////////////////////////////////////////

  void printVersion()
  {
    llvm::outs() << "Lava version " << LAVA_VERSION_STRING
                 << " (based on Clang " << CLANG_VERSION_STRING
                 << ")\n";
  }


//  std::string GetExecutablePath(const char *Argv0, bool CanonicalPrefixes) {
//    if (!CanonicalPrefixes)
//      return Argv0;
//
//    // This just needs to be some symbol in the binary; C++ doesn't
//    // allow taking the address of ::main however.
//    void *P = (void*) (intptr_t) GetExecutablePath;
//    return llvm::sys::fs::getMainExecutable(Argv0, P);
//  }

//  void LLVMErrorHandler(void *UserData, const std::string &Message,
//                        bool GenCrashDiag) {
//    DiagnosticsEngine &Diags = *static_cast<DiagnosticsEngine*>(UserData);
//
//    Diags.Report(diag::err_fe_error_backend) << Message;
//
//    // Run the interrupt handlers to make sure any special cleanups get done, in
//    // particular that we remove files registered with RemoveFileOnSignal.
//    llvm::sys::RunInterruptHandlers();
//
//    // We cannot recover from llvm errors.  When reporting a fatal error, exit
//    // with status 70 to generate crash diagnostics.  For BSD systems this is
//    // defined as an internal software error.  Otherwise, exit with status 1.
//    exit(GenCrashDiag ? 70 : 1);
//  }

//  int lava_main(ArrayRef<const char *> argv) {
//    std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());
//    IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
//
//    llvm::InitializeAllTargets();
//
//    // Buffer diagnostics from argument parsing so that we can output them using a
//    // well formed diagnostic object.
//    IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
//    TextDiagnosticBuffer *DiagsBuffer = new TextDiagnosticBuffer;
//    DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagsBuffer);
//    bool Success = CompilerInvocation::CreateFromArgs(Clang->getInvocation(), argv.begin(), argv.end(), Diags);
//
//    // Infer the builtin include path if unspecified.
//    if (Clang->getHeaderSearchOpts().UseBuiltinIncludes &&
//        Clang->getHeaderSearchOpts().ResourceDir.empty())
//      Clang->getHeaderSearchOpts().ResourceDir =
//      CompilerInvocation::GetResourcesPath(argv[0], (void *)(intptr_t)GetExecutablePath);
//
//    // Create the actual diagnostics engine.
//    Clang->createDiagnostics();
//    if (!Clang->hasDiagnostics())
//      return 1;
//    
//    // Set an error handler, so that any LLVM backend diagnostics go through our
//    // error handler.
//    llvm::install_fatal_error_handler(LLVMErrorHandler,
//                                      static_cast<void*>(&Clang->getDiagnostics()));
//    
//    DiagsBuffer->FlushDiagnostics(Clang->getDiagnostics());
//    if (!Success)
//      return 1;
//    
//    // Execute the frontend actions.
//    Success = ExecuteCompilerInvocation(Clang.get());
//    
//    // If any timers were active but haven't been destroyed yet, print their
//    // results now.  This happens in -disable-free mode.
//    llvm::TimerGroup::printAll(llvm::errs());
//
//    // Our error handler depends on the Diagnostics object, which we're
//    // potentially about to delete. Uninstall the handler now so that any
//    // later errors use the default handling behavior instead.
//    llvm::remove_fatal_error_handler();
//
//    // When running with -disable-free, don't do any destruction or shutdown.
//    if (Clang->getFrontendOpts().DisableFree) {
//      if (llvm::AreStatisticsEnabled() || Clang->getFrontendOpts().ShowStats)
//        llvm::PrintStatistics();
//      BuryPointer(std::move(Clang));
//      return !Success;
//    }
//
//    // Managed static deconstruction. Useful for making things like
//    // -time-passes usable.
//    llvm::llvm_shutdown();
//    
//    return !Success;
//  }

  void addTargetCmdArgs()
  {
    lava::forEachRegisteredTarget([] (const lava::TargetPlugin& target)
    {
      for(auto* category : target.registerCommandLineArguments())
        VisibleCategories.push_back(category);
    });
    std::sort(VisibleCategories.begin(), VisibleCategories.end());
    VisibleCategories.erase(std::unique(VisibleCategories.begin(), VisibleCategories.end()),
                            VisibleCategories.end());
  }

  void addTargetsToHelp()
  {
    std::vector<const lava::TargetPlugin*> targets;
    std::transform(lava::TargetRegistry::begin(),
                   lava::TargetRegistry::end(),
                   std::back_inserter(targets),
                   [] (const lava::TargetPlugin& target)
                   {
                     return &target;
                   });
    std::sort(targets.begin(), targets.end(), [] (const lava::TargetPlugin* a, const lava::TargetPlugin* b)
              {
                return a->name() < b->name();
              });
    std::for_each(targets.begin(), targets.end(), [] (const lava::TargetPlugin* target)
    {
      Target.getParser().addLiteralOption(target->name().data(), target, target->desc().data());
    });
  }

  void handleCommandLine(int argc, const char **argv)
  {
    addTargetsToHelp();
    addTargetCmdArgs();
    cl::HideUnrelatedOptions(VisibleCategories);
    cl::SetVersionPrinter(&printVersion);
    if(argc == 1)
    {
      cl::PrintHelpMessage(false, true);
    }
    cl::ParseCommandLineOptions(argc, argv, Overview);
    if(SourcePaths.getNumOccurrences() > 1 && Output == "-")
    {
      Output.error("cannot print to console with multiple input files");
      exit(1);
    }
  }

  void configurePreprocessorOptions(PreprocessorOptions& opts)
  {
    std::transform(Macros.begin(),
                   Macros.end(),
                   std::back_inserter(opts.Macros),
                   [] (const std::string& macro)
                   {
                     return std::make_pair(macro, false);
                   });
  }

  void configureHeaderSearchOptions(HeaderSearchOptions& opts)
  {
    for(auto& path : HeaderSearchPaths)
    {
      opts.AddPath(path, frontend::IncludeDirGroup::Angled, false, true);
    }
  }

  void configureFrontendOptions(FrontendOptions& opts)
  {
    std::transform(SourcePaths.begin(),
                   SourcePaths.end(),
                   std::back_inserter(opts.Inputs), [] (StringRef source)
                   {
                     return FrontendInputFile{getAbsolutePath(source), InputKind::IK_CXX};
                   });
    opts.OutputFile = Output;
  }

  lava::OutputOptions outputOptions()
  {
    lava::OutputOptions opts;
    opts.fileExt = Extension.getNumOccurrences() > 0 ? Extension : Target->extension();
    opts.fileExtFragment = ExtFragment + "." + opts.fileExt;
    opts.fileExtVertex = ExtVertex + "." + opts.fileExt;
    return opts;
  }

  lava::CodeGenOptions codeGenOptions()
  {
    return {};
  }
}

int main(int argc, const char* argv[])
{
  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  handleCommandLine(argc, argv);

  CompilerInstance compiler;
  compiler.createDiagnostics();
  compiler.getTargetOpts().Triple = llvm::sys::getProcessTriple();
  compiler.getLangOpts() = lava::langOptions();
  configureFrontendOptions(compiler.getFrontendOpts());
  configureHeaderSearchOptions(compiler.getHeaderSearchOpts());
  configurePreprocessorOptions(compiler.getPreprocessorOpts());

  auto target = Target->targetFromCommandLineArguments();
  lava::CodeGenAction action{*target, outputOptions(), codeGenOptions()};
  if(compiler.ExecuteAction(action))
  {
    compiler.clearOutputFiles(false);
    return 0;
  }
  else
  {
    compiler.clearOutputFiles(true);
    return 1;
  }
}
