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

void glsl::TypeNamePrinter::printFunctionName(FunctionDecl& decl, IndentWriter& w)
{
  _mangler->mangleCXXName(&decl, w.ostreamWithIndent());
}

void glsl::TypeNamePrinter::printCxxTypeName(QualType type, IndentWriter& w)
{
  type.print(w.ostreamWithIndent(), _mangler->getASTContext().getPrintingPolicy());
}

void glsl::TypeNamePrinter::printCxxFunctionName(FunctionDecl& decl, IndentWriter& w)
{
  decl.printQualifiedName(w.ostreamWithIndent());
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
  _typeNamePrinter.printCxxTypeName(type, _w);
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
  _typeNamePrinter.printCxxTypeName(type, _w);
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

glsl::FunctionBuilder::FunctionBuilder(FunctionDecl& decl, TypeNamePrinter& typeNamePrinter)
: _typeNamePrinter(typeNamePrinter)
, _decl(decl)
{
}

bool glsl::FunctionBuilder::setReturnType(QualType type)
{
  _returnType = type;
  return true;
}

bool glsl::FunctionBuilder::addParam(QualType type, llvm::StringRef identifier)
{
  _formalParams.push_back({type, identifier.str()});
  return true;
}

template<class F>
bool glsl::FunctionBuilder::pushScope(F director)
{
  if(_declString.empty())
  {
    // This is the first block in the function.
    // This means we know the entire signature and can emit the decl and header.
    buildProtoStrings();
  }
  _w << '{' << _w.endln();
  _w.increase();
  if(director())
  {
    _w.decrease();
    _w << '}' << _w.endln();
    return true;
  }
  else
    return false;
}

void glsl::FunctionBuilder::finalize()
{
  _w.ostream().flush();
}

void glsl::FunctionBuilder::buildProtoStrings()
{
  assert(!_returnType.isNull() && "return type not set");
  {
    // Build the decl first and re-use it for the definition as they are the same
    llvm::raw_string_ostream out{_declString};
    IndentWriter w{out};

    w << "// ";
    _returnType.print(w.ostream(), _decl.getASTContext().getPrintingPolicy());
    w << ' ';
    _typeNamePrinter.printCxxFunctionName(_decl, w);
    w << '(';
    bool first = true;
    for(const auto& param : _decl.params())
    {
      if(!first)
        w << ", ";
      first = false;
      param->print(w.ostream());
    }
    w << ')' << w.endln();

    _typeNamePrinter.printTypeName(_returnType, w);
    w << ' ';
    _typeNamePrinter.printFunctionName(_decl, w);
    w << '(';
    first = true;
    for(const auto& param : _formalParams)
    {
      if(!first)
        w << ", ";
      first = false;
      _typeNamePrinter.printTypeName(param.first, w);
      w << ' ' << param.second;
    }
    w << ')';
  }
  _defString = _declString;
  _declString += ";\n";

  _defString += '\n';
}

////////////////////////////////////////////////////////////////////////////////
// ModuleBuilder
//

glsl::ModuleBuilder::ModuleBuilder(ASTContext& ast)
: _typeNamePrinter(ast)
{
}

template<class Director>
bool glsl::ModuleBuilder::buildRecord(QualType type, Director director)
{
  RecordBuilder builder{type, _typeNamePrinter};
  auto success = director(builder);
  if(success)
  {
    _records.defs += builder.finalize();
  }
  return success;
}

template<class Director>
bool glsl::ModuleBuilder::buildFunction(FunctionDecl& decl, Director director)
{
  FunctionBuilder builder{decl, _typeNamePrinter};
  auto success = director(builder);
  if(success)
  {
    builder.finalize();
    _functions.decls += builder.declaration();
    _functions.defs += builder.definition();
  }
  return success;
}

std::string glsl::ModuleBuilder::moduleContent()
{
  return _records.defs + '\n' + _functions.decls + '\n' + _functions.defs;
}
