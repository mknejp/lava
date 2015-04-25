// RUN: %clang_cc1 %s -verify -pedantic -fsyntax-only -std=c++14

void invalid()
{
  { using err = float __attribute((matrix_type(1))); } // expected-error {{'matrix_type' attribute requires exactly 2 arguments}}
  { using err = float __attribute((matrix_type(1, 1, 1))); } // expected-error {{'matrix_type' attribute requires exactly 2 arguments}}
  { using err = float __attribute((matrix_type("", 1))); } // expected-error {{matrix_type attribute requires an integer constant}}
  { using err = float __attribute((matrix_type(1, ""))); } // expected-error {{matrix_type attribute requires an integer constant}}
  { using err = float __attribute((matrix_type(1, 1.f))); } // expected-error {{matrix_type attribute requires an integer constant}}
  { using err = float __attribute((matrix_type(1.0, 1))); } // expected-error {{matrix_type attribute requires an integer constant}}
  { using err = float __attribute((matrix_type(0, 1))); } // expected-error {{zero matrix row size}}
  { using err = float __attribute((matrix_type(1, 0))); } // expected-error {{zero matrix column size}}
  { using err = float __attribute((matrix_type(-1, 1))); } // expected-error {{matrix row size too large}}
  { using err = float __attribute((matrix_type(1, -1))); } // expected-error {{matrix column size too large}}
}
  
using mat2 = float __attribute((matrix_type(2, 2)));
using mat3 = float __attribute((matrix_type(3, 3)));
using mat4 = float __attribute((matrix_type(4, 4)));

using mat2x4 = float __attribute((matrix_type(2, 4)));

void foo()
{
}
