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
#include "clang/Format/Format.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Lava/LavaAction.h"
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

namespace cl = llvm::cl;
using namespace clang;
using namespace clang::tooling;

namespace {

  // All options must belong to locally defined categories for them to get shown
  // by -help. We explicitly hide everything else (except -help and -version).
  cl::OptionCategory LavaCategory("Lava Options");
  cl::OptionCategory LanguageCategory("Language Options");

  const cl::OptionCategory *VisibleCategories[] = {
    &LavaCategory, &LanguageCategory,
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

  ////////////////////////////////////////////////////////////////////////////////
  /// Lanugage Options

  cl::opt<std::string> Includes("I",
                                cl::desc("Add directory to include search path"),
                                cl::value_desc("value"),
                                cl::ZeroOrMore,
                                cl::Prefix,
                                cl::cat(LanguageCategory));
  cl::opt<std::string> Defines("D",
//                               cl::desc("Define a preprocessor token"),
                               cl::value_desc("name"),
                               cl::ZeroOrMore,
                               cl::Prefix,
                               cl::Hidden,
                               cl::cat(LanguageCategory));
  cl::opt<std::string> Undefines("U",
//                                 cl::desc("Undefine a preprocessor token"),
                                 cl::value_desc("name"),
                                 cl::ZeroOrMore,
                                 cl::Prefix,
                                 cl::Hidden,
                                 cl::cat(LanguageCategory));

  ////////////////////////////////////////////////////////////////////////////////
  /// General Options

  cl::list<std::string> SourcePaths(cl::Positional,
                                    cl::desc("[<sources>...]"),
                                    cl::ZeroOrMore/*,
                                    cl::cat(LavaCategory)*/);

//  cl::opt<bool> FinalSyntaxCheck("final-syntax-check",
//                                 cl::desc("Check for correct syntax after applying transformations"),
//                                 cl::init(false));
//
//  cl::opt<bool> SummaryMode("summary", cl::desc("Print transform summary"),
//                            cl::init(false), cl::cat(GeneralCategory));
//
//  cl::opt<std::string>
//  TimingDirectoryName("perf",
//                      cl::desc("Capture performance data and output to specified "
//                               "directory. Default: ./migrate_perf"),
//                      cl::ValueOptional, cl::value_desc("directory name"),
//                      cl::cat(GeneralCategory));
//
//  cl::opt<std::string> SupportedCompilers("for-compilers", cl::value_desc("string"),
//                                          cl::desc("Select transforms targeting the intersection of\n"
//                                                   "language features supported by the given compilers.\n"
//                                                   "Takes a comma-separated list of <compiler>-<version>.\n"
//                                                   "\t<compiler> can be any of: clang, gcc, icc, msvc\n"
//                                                   "\t<version> is <major>[.<minor>]\n"),
//                                          cl::cat(GeneralCategory));

  ////////////////////////////////////////////////////////////////////////////////
  /// GLSL Options

//  cl::opt<unsigned> EsslVersion("essl",
//                                cl::desc("Selects the GLSL ES code generator.\n"
//                                         "The value specifies the GLSL ES #version to generate for."),
//                                cl::Optional,
//                                cl::cat(LavaCategory));
//  cl::opt<unsigned> GlslVersion("glsl",
//                                cl::desc("Selects the GLSL code generator.\n"
//                                         "The value specifies the GLSL #version to generate for."),
//                                cl::Optional,
//                                cl::cat(LavaCategory));
  cl::opt<std::string> EsslVersion("target",
                                   cl::desc("Selects the target to generate code for."),
                                   cl::Optional,
                                   cl::cat(LavaCategory));

  ////////////////////////////////////////////////////////////////////////////////
  /// Output Options

  cl::opt<std::string> Output("o",
                              cl::desc("Specifies the output directory for the generated shader stage files."),
                              cl::Optional,
                              cl::cat(LavaCategory));
  cl::opt<std::string> ExtCompute("ext-comp",
                                  cl::desc("The extension to append to compute shader output files (default 'comp')."),
                                  cl::init("comp"),
                                  cl::Optional,
                                  cl::cat(LavaCategory));
  cl::opt<std::string> ExtFragment("ext-frag",
                                   cl::desc("The extension to append to fragment shader output files (default 'frag')."),
                                   cl::init("frag"),
                                   cl::Optional,
                                   cl::cat(LavaCategory));
  cl::opt<std::string> ExtVertex("ext-vert",
                                 cl::desc("The extension to append to vertex shader output files (default 'vert')."),
                                 cl::init("vert"),
                                 cl::Optional,
                                 cl::cat(LavaCategory));
  cl::opt<bool> StripExt("strip-ext",
                         cl::desc("If specified the input file extension is stripped before appending the stage specific extension."),
                         cl::init(false),
                         cl::cat(LavaCategory));

  ////////////////////////////////////////////////////////////////////////////////

  void printVersion() {
    llvm::outs() << "Lava version " << LAVA_VERSION_STRING
                 << " (based on Clang version " << CLANG_VERSION_STRING
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

}

int main(int argc_, const char **argv_) {
  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc_, argv_);

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  /*
  SmallVector<const char *, 256> argv;
  llvm::SpecificBumpPtrAllocator<char> ArgAllocator;
  std::error_code EC = llvm::sys::Process::GetArgumentVector(argv, llvm::makeArrayRef(argv_, argc_), ArgAllocator);
  if (EC) {
    llvm::errs() << "error: couldn't get arguments: " << EC.message() << '\n';
    return 1;
  }
*/
  cl::HideUnrelatedOptions(llvm::makeArrayRef(VisibleCategories));
  cl::SetVersionPrinter(&printVersion);


  CommonOptionsParser op(argc_, argv_, LavaCategory, Overview);
  ClangTool tool(op.getCompilations(), op.getSourcePathList());
  return tool.run(newFrontendActionFactory<lava::TestAction>().get());

//  std::vector<std::unique_ptr<ASTUnit>> ASTs;
//  tool.buildASTs(ASTs);
  // Parse options and generate compilations.
//  std::unique_ptr<CompilationDatabase> Compilations(FixedCompilationDatabase::loadFromCommandLine(argc_, argv_));
//  cl::ParseCommandLineOptions(argc_, argv_, Overview);

//  return lava_main(argv);


//  return 0;
}
