//===--- CodePrintingTools.cpp - Common utilities for text gen --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Lava/CodePrintingTools.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/Lava/IndentWriter.h"

using namespace clang;
using namespace lava;

namespace
{
  template<class F>
  std::string stringFromIndentWriter(F f)
  {
    std::string result;
    {
      llvm::raw_string_ostream out{result};
      IndentWriter w{out};
      f(w);
    }
    return result;
  }
}

TypeMangler::TypeMangler(ASTContext& ast)
: _mangler(ItaniumMangleContext::create(ast, ast.getDiagnostics()))
{
}

TypeMangler::TypeMangler(TypeMangler&&) = default;
TypeMangler& TypeMangler::operator=(TypeMangler&&) = default;
TypeMangler::~TypeMangler() = default;

void TypeMangler::mangleCxxTypeName(QualType type, IndentWriter& w) const
{
  _mangler->mangleTypeName(type, w.ostreamWithIndent());
}

void TypeMangler::mangleCxxDeclName(NamedDecl& decl, IndentWriter& w) const
{
  // TODO: ctro/dtor must be treated separately
  _mangler->mangleCXXName(&decl, w.ostreamWithIndent());
}

std::string clang::lava::TypeMangler::mangleCxxTypeName(QualType type) const
{
  return stringFromIndentWriter([&type, this] (IndentWriter& w)
  {
    mangleCxxTypeName(type, w);
  });
}

std::string clang::lava::TypeMangler::mangleCxxDeclName(NamedDecl& decl) const
{
  return stringFromIndentWriter([&decl, this] (IndentWriter& w)
  {
    mangleCxxDeclName(decl, w);
  });
}

void clang::lava::printCxxFunctionName(FunctionDecl& decl, IndentWriter& w)
{
  decl.printQualifiedName(w.ostreamWithIndent());
}

void clang::lava::printCxxFunctionProto(FunctionDecl& decl, IndentWriter& w)
{
  auto& policy = decl.getASTContext().getPrintingPolicy();
  decl.getReturnType().print(w.ostreamWithIndent(), policy);
  w << ' ';
  decl.printQualifiedName(w.ostream(), policy);
  w << '(';
  auto first = true;
  for(const auto& param: decl.params())
  {
    if(!first)
      w << ", ";
    first = false;
    param->print(w.ostream(), policy);
  }
  w << ')';
}

std::string clang::lava::printCxxFunctionName(FunctionDecl& decl)
{
  return stringFromIndentWriter([&decl] (IndentWriter& w)
  {
    printCxxFunctionName(decl, w);
  });
}

std::string clang::lava::printCxxFunctionProto(FunctionDecl& decl)
{
  return stringFromIndentWriter([&decl] (IndentWriter& w)
  {
    printCxxFunctionProto(decl, w);
  });
}

void clang::lava::printOperator(BinaryOperatorKind opcode, IndentWriter& w)
{
  switch(opcode)
  {
    case BO_PtrMemD:
    case BO_PtrMemI:
      llvm_unreachable("pointer-to-member not supported");
    case BO_Mul:       w << " * "; break;
    case BO_Div:       w << " / "; break;
    case BO_Rem:       w << " % "; break;
    case BO_Add:       w << " + "; break;
    case BO_Sub:       w << " - "; break;
    case BO_Shl:       w << " << "; break;
    case BO_Shr:       w << " >> "; break;
    case BO_LT:        w << " < "; break;
    case BO_GT:        w << " > "; break;
    case BO_LE:        w << " <= "; break;
    case BO_GE:        w << " >= "; break;
    case BO_EQ:        w << " == "; break;
    case BO_NE:        w << " != "; break;
    case BO_And:       w << " & "; break;
    case BO_Xor:       w << " ^ "; break;
    case BO_Or:        w << " | "; break;
    case BO_LAnd:      w << " && "; break;
    case BO_LOr:       w << " || "; break;
    case BO_Assign:    w << " = "; break;
    case BO_MulAssign: w << " *= "; break;
    case BO_DivAssign: w << " /= "; break;
    case BO_RemAssign: w << " %= "; break;
    case BO_AddAssign: w << " += "; break;
    case BO_SubAssign: w << " -= "; break;
    case BO_ShlAssign: w << " <<= "; break;
    case BO_ShrAssign: w << " >>= "; break;
    case BO_AndAssign: w << " &= "; break;
    case BO_XorAssign: w << " ^= "; break;
    case BO_OrAssign:  w << " |= "; break;
    case BO_Comma:     w << ", "; break;
  }
}

void clang::lava::printOperator(UnaryOperatorKind opcode, IndentWriter& w)
{
  switch(opcode)
  {
    case UO_PostInc: w << "++"; break;
    case UO_PostDec: w << "--"; break;
    case UO_PreInc:  w << "++"; break;
    case UO_PreDec:  w << "--"; break;
    case UO_Plus:    w << "+"; break;
    case UO_Minus:   w << "-"; break;
    case UO_Not:     w << "~"; break;
    case UO_LNot:    w << "!"; break;
    case UO_AddrOf:
    case UO_Deref:
    case UO_Real:
    case UO_Imag:
    case UO_Extension:
      llvm_unreachable("operator not supported");
  }
}

void clang::lava::printBoolLiteral(bool value, IndentWriter& w)
{
  w << (value ? "true" : "false");
}

void clang::lava::printFloatingLiteral(const FloatingLiteral& literal, PrintFloatingSuffix suffix, IndentWriter& w)
{
  SmallString<16> str;
  literal.getValue().toString(str);
  w << str;
  if (str.find_first_not_of("-0123456789") == StringRef::npos)
  {
    w << '.'; // Trailing dot in order to separate from ints.
  }
  if(suffix)
  {
    switch(literal.getType()->getAs<BuiltinType>()->getKind())
    {
      case BuiltinType::Half:
        if(suffix & PrintFloatingSuffixHalf)
        w << 'H';
        break;
      case BuiltinType::Float:
        if(suffix & PrintFloatingSuffixFloat)
          w << 'F';
        break;
      case BuiltinType::Double:
        // no suffix
        break;
      case BuiltinType::LongDouble:
        if(suffix & PrintFloatingSuffixLongDouble)
          w << 'L';
        break;
      default:
        llvm_unreachable("unexpected type for float literal");
    }
  }
}
