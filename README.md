# Lava
Lava is a project based on the Clang/LLVM toolset aiming to provide a unified C++ based graphics shading language. The ultimate goal is to have all shaders written in a single API-neutral and vendor-neutral language based on ISO-C++ (with certain restrictions due to the nature of the target environment).

It is in a very early state of development and more of an experiment (or proof-of-concept, if you like), worked on by one dude in his spare time. If everything goes according to plan it will be able to output SPIR-V, GLSL, ESSL, HLSL, MetalSL, and C++ headers with compatible data structures etc.

## Getting it
First follow the installation instructions at http://clang.llvm.org/get_started.html, (although I suggest using their Git mirrors instead of SVN) **except** use http://github.com/mknejp/lava instead of the official Clang repository, but don't run CMake just yet. Make sure all repositories are set to branch `release_37` as that is what Lava is currently being developed against.

Checkout http://github.com/mknejp/glslang into the folder `llvm/tools/clang/utils/glslang`.

Switch the Lava repository to branch `lava`.

You are now ready to run CMake.

## What can I do with it?
To play around build the `lava` target. Run the executable with `lava myfile.cpp --`. The exact output will vary depending on what I'm currently hacking on. It will probably be a mixture of the AST and/or GLSL and/or a SPIR-V dump. That's really it. Once the implementation gets a bit more stable output will be controlled with command line arguments. Also, expect crashes if the source contains stuff the code generators cannot handle yet. Restrict yourself to functions with basic expressions and `if/then/else` control flow. You will need a function marked with `vertex` or `fragment` to see anything since the frontend only processes symbols reachable from shader entry points.

GLSL (of no specific version) and SPIR-V are the two backends that are actively being worked on right now. Many parts of the GLSL backend will be re-usable in the other generators since their languages all revolve around a highly similar C-based syntax.

All the code generating stuff is in `include/Lava` and `lib/Lava` which you can experiment with. I try to make all the pieces as re-usable as possible. There were also some minor adjustments to Clang's Parser, AST and Sema modules to support function entry point attributes, matrices and other minor tweaks. But those adjustments are very incomplete as the primary focus right now is code generation. That's the reason I decided to put everything into a Clang fork instead of separating it.

## Did you say SPIR-V?
Indeed! Though it is still very early to have anything usable. The basic blocks are in place and progress is steadily going forward. Getting this done properly and soonish has the highest priority for me. You may see SPIR-V disassembly when running the `lava` tool.

## Silly example
In order to see any output you currently require an entry point that calls your example code. Entry point code generation is not yet supported so you will only see the code for functions you call from there. Here is what a very silly example would generate from the following input:
```cpp
namespace N
{
	int foo(int& a)
	{
		int x;
		if(true)
		{
			if(true)
			{
				x = 2;
			}
			else
			{
				x = 3;
			}
		}
		return x;
	}
}

vertex void transform()
{
	int x;
	N::foo(x);
}
```
This currently produces the following two outputs (left column SPIR-V, right column GLSL)
```
// Module Version 99                                                // int N::foo(int &a)
// Generated by (magic number): bb                                  int _ZN1N3fooERi(inout int a);
// Id's are bound by 21
                                                                    // int N::foo(int &a)
                              MemoryModel Logical GLSL450           int _ZN1N3fooERi(inout int a)
                              Name 5  "_ZN1N3fooERi"                {
                              Name 4  "a"                             int x;
               1:             TypeInt 32 1                            if(true)
               2:             TypePointer Function 1(int)             {
               3:             TypeFunction 1(int) 2(ptr)                if(true)
               8:             TypeBool                                  {
               9:     8(bool) ConstantTrue                                x = 2;
              14:      1(int) Constant 2                                }
              16:      1(int) Constant 3                                else
              19:             TypeVoid                                  {
 5(_ZN1N3fooERi):      1(int) Function None 3                             x = 3;
            4(a):      2(ptr) FunctionParameter                         }
               6:             Label                                   }
               7:      1(int) Undef                                   return x;
                              SelectionMerge 11 None                }
                              BranchConditional 9 10 11
              10:               Label
                                SelectionMerge 13 None
                                BranchConditional 9 12 15
              12:                 Label
                                  Branch 13
              15:                 Label
                                  Branch 13
              13:               Label
              17:      1(int)   Phi 14 12 16 15
                                Branch 11
              11:             Label
              18:      1(int) Phi 7 6 17 13
                              ReturnValue 18
                              FunctionEnd
```

## The Language
Lava is based on the ISO-C++14 standard with some restrictions applied that aren't really suitable for a GPU stream processor (exceptions, RTTI, virtual, ...). There are also some limitations that are dictated by the capabilities of the supported target languages (no pointers, reference captures, etc). This all still needs fleshing out as the code generators mature.

This code defines a few vector/matrix types
```cpp
template<class T, int N>
using vec = T __attribute__((ext_vector_type(N)));

using vec2 = vec<float, 2>;
using vec3 = vec<float, 3>;
using vec4 = vec<float, 4>;

template<class T, int Rows, int Cols>
using mat = T __attribute__((matrix_type(Rows, Cols)));

using mat2x2 = mat<float, 2, 2>;
using mat2 = mat2x2;
using mat3x3 = mat<float, 3, 3>;
using mat3 = mat3x3;
using mat4x4 = mat<float, 4, 4>;
using mat4 = mat4x4;
```
Don't worry, those ugly attributes are something that can be hidden in a standard library header. And yes, it's row-major matrix notation (not to be confused with layout!) because that is what every damn mathematics textbook in the world is teaching students. And therefore the following
```cpp
auto m = mat3{1, 2, 3,
              4, 5, 6,
              7, 8, 9}
```
does exactly what you expect it to! No need to transpose the thing in your head. Let the rest be a problem for the compiler, not the user. There is currently nothing you can do with matrices, they are just there to crete AST nodes so I have input to test code generation with.

To declare an entry point:
```cpp
int foo() { return 1; }

vertex void transform() { foo(); }
fragment void shade() { foo(); }
```
Remember that the frontend currently filters out all symbols that are not reachable from entry points. This is intended to be an option in the future.

## What next?
Probably a lot of patience (unless you wish to participate), since as already mentioned I am doing this alone in my spare time and this is a very ambitious project. I hope you like what you see so far. Let me know what you think.

## Why "Lava"?
Because with Mantle, Vulkan and Metal it seems like geology is a new synonym for computer graphics. And for me lava is the first thing that comes to mind when thinking about volcanos.

## Status
This overview represents the capabilities of the frontend and code genrators and is held as up-to-date as my memory and attention span allow.

### C++ frontend status
These are the changes planned for the frontend to support all the stuff for graphics programming.
I have investigated the Clang code some of them but most are here only for reference.
- [ ] Disable or adjust language features that are not supported (directly) by any code generator or backend
  - [x] Exceptions
  - [x] RTTI
  - [ ] `virtual`
  - [ ] `goto`
  - [ ] `union`
  - [ ] **pointers**: Neither the SPIR-V logical memory model nor GLSL support pointer arithmetic. But refernces can probably be made to work. Shader inputs declared as pointers or unsized arrays can probably be transformed accordingly.
  - [ ] *pointer-to-member operator*
  - [ ] *address-of operator*
  - [ ] *dereference operator*
  - [ ] Conditional operator used as assignment target `int x, y; (cond ? x : y) = 1;`: This one is difficult. The syntax is invalid in GLSL because the conditional operator only generates rvalues, but it can be implemented in SPIR-V. If this were to be supported for all backends then it needs some AST transformation.
  - [ ] *const_cast*
  - [ ] *reinterpret_cast*
  - [ ] Reference captures in closures: Neither GLSL nor SPIR-V in the logical memory model allow pointers/references as structure members. This rule *might* be relaxed in function-local lambdas since they are most likely inlining candidates.
  - [ ] Character literals: Could be translated to integer literals.
  - [ ] String literals: Could be translated to constant integer arrays.
  - [ ] Nested switch cases: C++ allows the `case` statement to be placed within nested control flow. GLSL does not and SPIR-V forbids branching into control flow structures.
- [ ] Vectors
  - [x] Dedicated vector types: Clang already has `__attribute((ext_vector_type))` that we can re-use.
  - [ ] GLSL-style initialization from other vectors `vec4(v1, v2)`: This isn't working properly yet, but there seems to be some special case code for OpenCL that should be examined.
  - [ ] Arithmetic operations with scalars/matrices
  - [x] swizzling: already supported for `__attribute((ext_vector_type))`
  - [x] `.rgba` swizzle
- [ ] Matrices
  - [x] Dedicated matrix type `__attribute((matrix_type))`
  - [ ] Construction from rows/columns/elements
  - [ ] Arithmetic operations with scalars/vectors
  - [ ] Extract/insert columns/rows
- [ ] Samplers
- [ ] Images
- [ ] Uniforms (UBO, SSBO)
- [ ] Compute kernel workgroup stuff
- [ ] Shader stage entry point annotations: Mark a function as being the entry point of a shader stage. This section only cares about the ability to annotate, not sanity checking of arguments.
  - [x] `vertex`
  - [x] `fragment`
  - [ ] `geometry`
  - [ ] `hull`
  - [ ] `domain`
  - [ ] `compute`
- [ ] Annotation of function parameters / record members with builtin semantics
- [ ] Validation of input/output arguments to/from shader stages
  - [ ] `vertex`
  - [ ] `fragment`
  - [ ] `geometry`
  - [ ] `hull`
  - [ ] `domain`
  - [ ] `compute`
- [ ] `discard`
- [ ] Builtin functions
  - [ ] The minimal set of functions supported in SPIR-V without extensions (`dot()`, `transpose()`, etc).
  - [ ] Some way to control availability of builtin functions depending on backend capabilities
- [ ] Language feature selection based on backend capabilities (i.e. GLSL version, extensions, etc.)

### Code Generation Status
This is a matrix of how far the code generators are and what work is still outstanding.
Legend:
- :x: Not implemented and not even started work on it yet
- :white_check_mark: *Should* be feature complete
- :warning: Partially done but not complete yet
- :collision: Even though code can be generated it is a feature that is dependend on the backend capabilities (like GLSL vs ESSL) or requires an extension to work. This needs addressing once feature-selection is a thing.

| Builtin types | SPIR-V | GLSL | HLSL  | MetalSL |
| ------------- |:------:|:----:|:-----:|:-------:|
| bool | :white_check_mark: | :white_check_mark: | :x: | :x: |
| (u)int16 | :collision: | :collision: | :x: | :x: |
| (u)int32 | :white_check_mark: | :white_check_mark: | :x: | :x: |
| (u)int64 | :collision: | :collision: | :x: | :x: |
| float16 | :collision: | :collision: | :x: | :x: |
| float32 | :white_check_mark: | :white_check_mark: | :x: | :x: |
| float64 | :collision: | :collision: | :x: | :x: |
| vector | :white_check_mark: | :white_check_mark: | :x: | :x: |
| matrix | :white_check_mark: | :white_check_mark: | :x: | :x: |
| arrays | :white_check_mark: | :white_check_mark: | :x: | :x: |

| Record types      | SPIR-V | GLSL | HLSL  | MetalSL |
| ---------------- |:------:|:----:|:-----:|:-------:|
| scalar members | :white_check_mark: | :white_check_mark: | :x: | :x: |
| array members | :white_check_mark: | :white_check_mark: | :x: | :x: |
| record members | :white_check_mark: | :white_check_mark: | :x: | :x: |
| base classes | :white_check_mark: | :white_check_mark: | :x: | :x: |
| static members | :x: | :x: | :x: | :x: |
| lambda closures | :white_check_mark: | :white_check_mark: | :x: | :x: |

| Member functions      | SPIR-V | GLSL | HLSL  | MetalSL |
| ---------------- |:------:|:----:|:-----:|:-------:|
| constructors | :x: | :x: | :x: | :x: |
| destructors | :x: | :x: | :x: | :x: |
| overloaded operators | :x: | :x: | :x: | :x: |
| non-static methods | :x: | :x: | :x: | :x: |
| static methods | :x: | :x: | :x: | :x: |
| lambda invoker | :x: | :x: | :x: | :x: |

| Functions | SPIR-V | GLSL | HLSL  | MetalSL |
| ---------------- |:------:|:----:|:-----:|:-------:|
| by-value params | :white_check_mark: | :white_check_mark: | :x: | :x: |
| by-ref params | :warning: (no write on return) | :white_check_mark: | :x: | :x: |
| by-const-ref params | :white_check_mark: | :white_check_mark: | :x: | :x: |

| Statements | SPIR-V | GLSL | HLSL  | MetalSL |
| ---------------- |:------:|:----:|:-----:|:-------:|
| `break` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| `continue` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| `discard` | :x: | :x: | :x: | :x: |
| `do` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| `for` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| `if`/`else` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| `return` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| `switch` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| `while` | :white_check_mark: | :white_check_mark: | :x: | :x: |
| initalized local | :white_check_mark: | :white_check_mark: | :x: | :x: |
| uninitalized local | :white_check_mark: | :white_check_mark: | :x: | :x: |

| Expressions | SPIR-V | GLSL | HLSL  | MetalSL |
| ---------------- |:------:|:----:|:-----:|:-------:|
| unary operator scalars | :white_check_mark: | :white_check_mark: | :x: | :x: |
| unary operator vectors | :x: | :x: | :x: | :x: |
| binary operator scalars | :white_check_mark: | :white_check_mark: | :x: | :x: |
| binary operator vectors | :x: | :x: | :x: | :x: |
| binary operator matrices | :x: | :x: | :x: | :x: |
| conditional operator | :x: | :x: | :x: | :x: |
| variable reference | :white_check_mark: | :white_check_mark: | :x: | :x: |
| member access | :x: | :x: | :x: | :x: |
| array subscript | :x: | :x: | :x: | :x: |
| swizzle | :x: | :x: | :x: | :x: |
| calls | :x: | :x: | :x: | :x: |
| atomics | :x: | :x: | :x: | :x: |
| casts (see below) | :x: | :x: | :x: | :x: |

| Casts | SPIR-V | GLSL | HLSL  | MetalSL |
| ---------------- |:------:|:----:|:-----:|:-------:|
| lvalue-to-rvalue | :white_check_mark: | :white_check_mark: | :x: | :x: |
| ... | :x: | :x: | :x: | :x: |

| Other | SPIR-V | GLSL | HLSL  | MetalSL |
| ---------------- |:------:|:----:|:-----:|:-------:|
| destructor calls for locals | :x: | :x: | :x: | :x: |
| entry points / main function | :x: | :x: | :x: | :x: |
| shader input / output | :x: | :x: | :x: | :x: |
| export / import functions | :x: | :x: | :x: | :x: |
| export / import variables | :x: | :x: | :x: | :x: |
| static initialization | :x: | :x: | :x: | :x: |
| backend versions | :x: | :x: | :x: | :x: |
| extensions | :x: | :x: | :x: | :x: |
| ... | :x: | :x: | :x: | :x: |
