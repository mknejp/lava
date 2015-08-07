//===- Version.h - Lava Version Number --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines version macros and version-related utility functions
/// for Clang.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LAVA_BASIC_VERSION_H
#define LLVM_LAVA_BASIC_VERSION_H

#include "clang/Basic/Version.h"
#include "llvm/ADT/StringRef.h"

/// TODO: move to generated file
#define LAVA_VERSION 0.1
#define LAVA_VERSION_MAJOR 0
#define LAVA_VERSION_MINOR 1
//#define LAVA_VERSION_PATCHLEVEL 0

#ifdef LAVA_VERSION_PATCHLEVEL
#define LAVA_MAKE_VERSION_STRING(X,Y,Z) CLANG_MAKE_VERSION_STRING2(X.Y.Z)
#define LAVA_VERSION_STRING LAVA_MAKE_VERSION_STRING(LAVA_VERSION_MAJOR, LAVA_VERSION_MINOR, LAVA_VERSION_PATCHLEVEL)
#else
#define LAVA_MAKE_VERSION_STRING(X,Y) CLANG_MAKE_VERSION_STRING2(X.Y)
#define LAVA_VERSION_STRING LAVA_MAKE_VERSION_STRING(LAVA_VERSION_MAJOR, LAVA_VERSION_MINOR)
#endif

#endif // LLVM_LAVA_BASIC_VERSION_H
