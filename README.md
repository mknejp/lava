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

## The Language
Lava is based on the ISO-C++14 standard with some restrictions applied that aren't really suitable for a GPU stream processor (exceptions, RTTI, virtual, ...). There are also some limitations that are dictated by the capabilities of the supported target languages (no pointers, arbitrary references, reference captures, etc). This all still needs fleshing out as the code generators mature.

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

## Did you say SPIR-V?
Indeed! Though it is still very early to have anything usable. The basic blocks are in place and progress is steadily going forward. Getting this done properly and soonish has the highest priority for me. You may see SPIR-V disassembly when running the `lava` tool.

## What next?
Probably a lot of patience, since as already mentioned I am doing this alone in my spare time and this is a very ambitious project. I hope you like what you see so far. Let me know what you think.

## Why "Lava"?
Because with Mantle, Vulkan and Metal it seems like geology is a new synonym for computer graphics. And for me lava is the first thing that comes to mind when thinking about volcanos.
