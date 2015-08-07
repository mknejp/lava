//===--- ModuleBuilder_GLSL.cpp - GLSL Code Gen -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Lava/ModuleBuilder_GLSL.h"

#include "clang/AST/DeclCXX.h"
#include "clang/Lava/ModuleBuilder.h"

using namespace clang;
using namespace lava;

using llvm::StringRef;

ModuleBuilder clang::lava::glsl::createModuleBuilder(ASTContext& ast)
{
  return lava::ModuleBuilder::create<glsl::ModuleBuilder>(ast);
}

////////////////////////////////////////////////////////////////////////////////
// TypeNamePrinter
//

void glsl::TypeNamePrinter::printTypeName(QualType type, IndentWriter& w)
{
  if(auto* arr = type->getAsArrayTypeUnsafe())
  {
    printTypeName(arr->getElementType(), w);
  }
  else if(auto* builtin = type->getAs<BuiltinType>())
  {
    switch(builtin->getKind())
    {
      case clang::BuiltinType::Void:   w << "void"; break;
      case clang::BuiltinType::Bool:   w << "bool"; break;
      case clang::BuiltinType::Int:    w << "int"; break;
      case clang::BuiltinType::UInt:   w << "uint"; break;
      case clang::BuiltinType::Float:  w << "float"; break;
      case clang::BuiltinType::Double: w << "double"; break;
      default:
        llvm_unreachable("invalid builtin type");
    }
  }
  else if(auto* vector = type->getAs<ExtVectorType>())
  {
    if(auto* builtin = vector->getElementType()->getAs<BuiltinType>())
    {
      switch(builtin->getKind())
      {
        case clang::BuiltinType::Bool:  w << "bvec"; break;
        case clang::BuiltinType::Int:   w << "ivec"; break;
        case clang::BuiltinType::UInt:  w << "uvec"; break;
        case clang::BuiltinType::Float: w << "vec"; break;
        default:
          llvm_unreachable("invalid vector element type");
      }
      printDimensionality(vector->getNumElements(), w);
    }
    else
      llvm_unreachable("invalid vector element type");
  }
  else if(auto* matrix = type->getAs<MatrixType>())
  {
    if(auto* builtin = matrix->getElementType()->getAs<BuiltinType>())
    {
      switch(builtin->getKind())
      {
        case clang::BuiltinType::Float: w << "mat"; break;
        case clang::BuiltinType::Double: w << "dmat"; break;
        default:
          llvm_unreachable("invalid matrix element type");
      }
      // GLSL matrix notation is column first
      printDimensionality(matrix->getNumColumns(), w);
      w << 'x';
      printDimensionality(matrix->getNumRows(), w);
    }
    else
      llvm_unreachable("invalid matrix element type");
  }
  else if(type->isRecordType())
  {
    _mangler->mangleTypeName(type, w.ostreamWithIndent());
  }
  else
    llvm_unreachable("invalid GLSL type");
}

void glsl::TypeNamePrinter::printCxxDiagnosticTypeName(QualType type, IndentWriter& w)
{
  type.print(w.ostreamWithIndent(), _mangler->getASTContext().getPrintingPolicy());
}

void glsl::TypeNamePrinter::printDimensionality(unsigned n, IndentWriter& w)
{
  switch(n)
  {
    case 2: w << '2'; break;
    case 3: w << '3'; break;
    case 4: w << '4'; break;
    default:
      llvm_unreachable("invalid vector/matrix dimensionality");
  }
}

////////////////////////////////////////////////////////////////////////////////
// RecordBuilder
//

glsl::RecordBuilder::RecordBuilder(QualType type, TypeNamePrinter& typeNamePrinter)
: _typeNamePrinter(typeNamePrinter)
, _printedBasesHeader(false)
, _printedFieldsHeader(false)
, _printedCapturesHeader(false)
{
  assert(type->getAsCXXRecordDecl() && "not a record!");

  _w << "// ";
  _typeNamePrinter.printCxxDiagnosticTypeName(type, _w);
  _w << _w.endln() << "struct ";
  _typeNamePrinter.printTypeName(type, _w);
  _w << _w.endln() << '{' << _w.endln();
  _w.increase();
}

bool glsl::RecordBuilder::addBase(QualType type, unsigned index)
{
  if(!_printedBasesHeader)
  {
    IndentWriter::PushOutdent po{_w};
    _w << "// bases:" << _w.endln();
    _printedBasesHeader = true;
  }
  _typeNamePrinter.printTypeName(type, _w);
  _w << ' ' << "_base_" << index << "; // ";
  _typeNamePrinter.printCxxDiagnosticTypeName(type, _w);
  _w << _w.endln();
  return true;
}

bool glsl::RecordBuilder::addField(QualType type, llvm::StringRef identifier)
{
  if(!_printedFieldsHeader && _printedBasesHeader)
  {
    IndentWriter::PushOutdent po{_w};
    _w << "// fields:" << _w.endln();
    _printedFieldsHeader = true;
  }
  printFieldImpl(type, identifier);
  return true;
}

bool glsl::RecordBuilder::addCapture(QualType type, llvm::StringRef identifier)
{
  if(!_printedCapturesHeader)
  {
    IndentWriter::PushOutdent po{_w};
    _w << "// captures:" << _w.endln();
    _printedCapturesHeader = true;
  }
  printFieldImpl(type, identifier);
  return true;
}

std::string glsl::RecordBuilder::finalize()
{
  _w.decrease();
  _w << "};" << _w.endln();
  _ostream.flush();
  return std::move(_def);
}

void glsl::RecordBuilder::printFieldImpl(QualType type, llvm::StringRef identifier)
{
  _typeNamePrinter.printTypeName(type, _w);
  _w << ' ' << identifier;
  if(auto* arr = dyn_cast_or_null<ConstantArrayType>(type->getAsArrayTypeUnsafe()))
  {
    _w << '[' << arr->getSize() << ']';
    type = arr->getElementType();
  }
  _w << ';';
  if(type->isRecordType())
  {
    _w << " // ";
    _typeNamePrinter.printTypeName(type, _w);
  }
  _w << _w.endln();
}

////////////////////////////////////////////////////////////////////////////////
// GlslFunctionBuilder
//

//namespace
//{
//  class GlslFunctionBuilder
//  {
//  public:
//    GlslFunctionBuilder(FunctionDecl& decl, MangleContext& mangler,
//                        ShaderTranslator& translator, ASTContext& ast);
//
//    void finalize();
//
//    std::string declaration() { return std::move(_decl); }
//    std::string definition() { return std::move(_def); }
//
//  private:
//    std::string _decl;
//    std::string _def;
//    raw_string_ostream _ostream{_def};
//    IndentWriter _w{_ostream};
//    MangleContext& _mangler;
//    ShaderTranslator& _translator;
//    ASTContext& _ast;
//  };
//}
//
//GlslFunctionBuilder::GlslFunctionBuilder(FunctionDecl& decl, MangleContext& mangler,
//                                         ShaderTranslator& translator, ASTContext& ast)
//: _mangler(mangler)
//, _translator(translator)
//, _ast(ast)
//{
//  {
//    // Build the decl first and re-use it for the definition as they are the same
//    raw_string_ostream out{_decl};
//    out << "// ";
//    decl.getNameForDiagnostic(out, _ast.getPrintingPolicy(), true);
//    out << '\n';
//
//    IndentWriter w{out};
//    printTypeName(decl.getReturnType(), mangler, translator, w);
//    w << ' ';
//    _mangler.mangleCXXName(&decl, w.ostream());
//    w << '(';
//    bool first = true;
//    for(const auto* param : decl.params())
//    {
//      if(!first)
//        w << ", ";
//      first = false;
//      printTypeName(param->getType(), mangler, translator, w);
//      w << param->getName();
//    }
//    w << ')';
//  }
//  _def = _decl;
//  _decl += ";\n";
//
//  _def += '\n';
//}

////////////////////////////////////////////////////////////////////////////////
// ModuleBuilder
//

glsl::ModuleBuilder::ModuleBuilder(ASTContext& ast)
: _typeNamePrinter(ast)
{
}

bool glsl::ModuleBuilder::buildRecord(QualType type, std::function<void(lava::RecordBuilder&)>& blueprint)
{
  RecordBuilderImpl<RecordBuilder> builder{type, _typeNamePrinter};
  blueprint(builder);
  if(builder.success())
  {
    _records.defs += builder->finalize();
  }
  return builder.success();
}

//FunctionBuilder& GlslModuleBuilder::beginFunction(FunctionDecl& decl)
//{
//  _functions.builder.emplace(decl);
//  return *_functions.builder;
//}

//void GlslModuleBuilder::endFunction()
//{
//  assert(_functions.builder && "beginFunction() not called before endFunction()");
//  _functions.decls += _functions.builder->declaration();
//  _functions.defs += _functions.builder->definition();
//  _functions.builder.reset();
//}

std::string glsl::ModuleBuilder::moduleContent()
{
  return _records.defs + _functions.decls + _functions.defs;
}
