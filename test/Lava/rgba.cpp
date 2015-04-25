// RUN: %clang_cc1 %s -verify -pedantic -fsyntax-only -std=c++14

typedef int ivec2 __attribute((ext_vector_type(2)));
typedef int ivec3 __attribute((ext_vector_type(3)));
typedef int ivec4 __attribute((ext_vector_type(4)));

typedef float vec2 __attribute((ext_vector_type(2)));
typedef float vec3 __attribute((ext_vector_type(3)));
typedef float vec4 __attribute((ext_vector_type(4)));

void foo() {
  vec4 v4 = vec4{1, 2, 3, 4};
  vec2 v2 = vec2{5, 6};
  v4.xr; // expected-error {{vector component groups 'xyzw' and 'rgba' cannot be mixed}}
  v2.a; // expected-error {{vector component access exceeds type 'vec2'}}
  v2.rgba; // expected-error {{vector component access exceeds type 'vec2'}}
  v4.r; // expected-warning {{expression result unused}}
  v4.rg; // expected-warning {{expression result unused}}
  
  vec3 v3 = v4.rgb;
  v2 = v4.rr;
}
