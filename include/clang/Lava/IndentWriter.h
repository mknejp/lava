//===--- LavaFrontendAction.h - Lava information gathering ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LAVA_INDENTWRITER_H
#define LLVM_CLANG_LAVA_INDENTWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace clang
{
namespace lava
{
  class IndentWriter;
  template<class F>
  class GenericStreamInserter;

  template<class F>
  GenericStreamInserter<typename std::decay<F>::type> makeStreamInserter(F&& f);

} // end namespace lava
} // end namespace clang

class clang::lava::IndentWriter
{
public:
  class PushIndent;
  class PushOutdent;
  class EndLine;

  IndentWriter(llvm::raw_ostream& ostream, int indentSpaces = 2)
  : _indentSpaces(indentSpaces), _ostream(&ostream)
  { }
  IndentWriter(const IndentWriter&) = delete;
  IndentWriter(IndentWriter&&) = delete;
  IndentWriter& operator=(const IndentWriter&) = delete;
  IndentWriter& operator=(IndentWriter&&) = delete;

  void increase(int levels = 1) { _level += levels; }
  void decrease(int levels = 1) { _level = std::max(0, _level - levels); }

  EndLine endln();

  llvm::raw_ostream& ostream()
  {
    return *_ostream;
  }

  /// Same as ostream() but inserts indentation if at beginning of line
  llvm::raw_ostream& ostreamWithIndent()
  {
    if(_newLine)
    {
      printIndent();
      _newLine = false;
    }
    return *_ostream;
  }

  void printIndent()
  {
    _ostream->indent(_indentSpaces * _level);
  }

  template<class F>
  void braced(F&& f);

  template<class F>
  void bracedSemi(F&& f);

  template<class F>
  void nested(int levels, F&& f);

  template<class F>
  void nested(F&& f) { return nested(1, std::move(f)); }

private:
  bool _newLine = true;
  int _level = 0;
  int _indentSpaces;
  llvm::raw_ostream* _ostream;
};

class clang::lava::IndentWriter::EndLine
{
public:
  EndLine(IndentWriter& w) : _w(w) { }

private:
  IndentWriter& _w;
  friend IndentWriter& operator<<(llvm::raw_ostream& out, EndLine ln)
  {
    out << '\n';
    ln._w._newLine = true;
    return ln._w;
  }
};

class clang::lava::IndentWriter::PushIndent
{
public:
  PushIndent(IndentWriter& w, int levels = 1) : _levels(levels), _w(w) { w.increase(_levels); }
  ~PushIndent() { _w.decrease(_levels); }

private:
  int _levels;
  IndentWriter& _w;
};

class clang::lava::IndentWriter::PushOutdent
{
public:
  PushOutdent(IndentWriter& w, int levels = 1) : _levels(levels), _w(w) { w.decrease(_levels); }
  ~PushOutdent() { _w.increase(_levels); }

private:
  int _levels;
  IndentWriter& _w;
};

namespace clang
{
namespace lava
{
  template<class T>
  IndentWriter& operator<<(IndentWriter& w, T&& x)
  {
    w.ostreamWithIndent() << std::forward<T>(x);
    return w;
  }

  inline IndentWriter& operator<<(IndentWriter& w, IndentWriter::EndLine ln)
  {
    return w.ostream() << ln;
  }
} // end namespace lava
} // end namespace clang

inline auto clang::lava::IndentWriter::endln() -> EndLine
{
  return {*this};
}

template<class F>
void clang::lava::IndentWriter::braced(F&& f)
{
  (*this) << '{';
  nested(1, std::forward<F>(f));
  (*this) << '}';
}

template<class F>
void clang::lava::IndentWriter::bracedSemi(F&& f)
{
  braced(std::forward<F>(f));
  (*this) << ';';
}

template<class F>
void clang::lava::IndentWriter::nested(int levels, F&& f)
{
  (*this) << endln();
  PushIndent push(*this, levels);
  std::forward<F>(f)();
}

template<class F>
class clang::lava::GenericStreamInserter
{
public:
  GenericStreamInserter(F f) : _f(std::move(f)) { }

  friend IndentWriter& operator<<(IndentWriter& w, const GenericStreamInserter<F>& ins)
  {
    ins._f(w);
    return w;
  }
  friend IndentWriter& operator<<(IndentWriter& w, GenericStreamInserter<F>&& ins)
  {
    std::move(ins._f)(w);
    return w;
  }

private:
  F _f;
};

template<class F>
auto clang::lava::makeStreamInserter(F&& f)
-> GenericStreamInserter<typename std::decay<F>::type>
{
  return {std::forward<F>(f)};
}


#endif // LLVM_CLANG_LAVA_INDENTWRITER_H
