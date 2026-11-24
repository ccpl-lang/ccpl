# CCPL

**Cool Compilable Programming Language** is a small, Lua flavored language that
compiles straight to a native executable. There is no interpreter, no bytecode,
and no virtual machine.

Version 0.4.0.

CCPL mixes a dynamic core with escape hatches to native C types, pointers, and
`malloc`. Source is lowered to C, then TinyCC turns that C into a native program
in milliseconds.

```ccpl
say("hello, world")

func fib(n)
    if n <= 1 then
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

for i = 1, 10 do
    say("fib", i, fib(i))
end
```

> In one line: a small language that feels like Lua and ships a native program.

## Contents

* [Why CCPL](#why-ccpl)
* [Requirements](#requirements)
* [Quick start](#quick-start)
* [Command line](#command-line)
* [Language tour](#language-tour)
* [Foreign function interface](#foreign-function-interface)
* [Package manager](#package-manager)
* [Examples](#examples)
* [Building from source](#building-from-source)
* [Testing](#testing)
* [Project layout](#project-layout)
* [Related repositories](#related-repositories)
* [License](#license)

## Why CCPL

* **Truly compiled.** Source becomes C, and TinyCC emits a native program. No
  runtime interpreter is involved.
* **Small and fast to build.** A whole program compiles in a few milliseconds.
* **Dynamic by default.** Numbers are doubles, with strings, booleans, and
  `nil`, plus Lua style `and`, `or`, and `not`.
* **Native when needed.** `i8` to `i64`, `u8` to `u64`, `f32`, `f64`, `bool`,
  `char`, `ptr`, and `cstr`, plus address of, dereference, indexing, and
  `malloc` and `free`.
* **Foreign functions.** Call C and C++ symbols from DLLs, and call Python
  functions or evaluate Python code. See [docs/ffi.md](docs/ffi.md).
* **One compiler file.** The compiler is a single C source with no dependencies
  beyond the Windows API and TinyCC.

## Requirements

> CCPL targets Windows x64, and the toolchain bundle includes TinyCC.

* Windows 10 or newer, on x64.
* TinyCC, either the bundled toolchain or a local install. The easy route is
  the ready to run [toolchain repository](https://github.com/ccpl-lang/ccpl-toolchain).
* Python and a C++ compiler are optional, and only needed for the Python and
  C++ foreign function features.

## Quick start

### Option A: ready to run toolchain

Download or clone the
[toolchain repository](https://github.com/ccpl-lang/ccpl-toolchain), then run:

```bat
coolc.bat hello.ccpl --run
```

The bundle also contains `clm.exe`, the package manager, and `clm.bat`.

### Option B: build the compiler yourself

```bat
git clone https://github.com/ccpl-lang/ccpl.git
cd ccpl
build.bat
build\coolc.exe examples\hello.ccpl --run
```

`build.bat` looks for the TinyCC driver in this order: `%TCC%`,
`build\tcc\tcc.exe`, `..\ccpl-toolchain\tcc\tcc.exe`, then `tcc` on `PATH`.

## Command line

```text
coolc <source.ccpl> [-o out.exe] [--run] [--fast] [--show-c]
```

* `-o, --output <file>` sets the output program path. The default is the source
  name with `.exe`.
* `--run` runs the program after a successful build.
* `--fast` compiles the generated C with `-O2 -s`.
* `--show-c` prints the generated C and exits. This is the best way to see how a
  program is lowered.
* `--version, -v` prints the compiler version.
* `-h, --help` prints usage.

## Language tour

### Comments

```ccpl
-- a line comment
--[[ a
     block comment ]]
```

### Values and variables

Dynamic values are numbers, strings, booleans, and `nil`.

```ccpl
var greeting = "hello"
variable count = 3
var flag = true
var nothing = nil

count = count + 1
say(greeting, count, flag, nothing)
```

A declaration without a value starts as `nil`:

```ccpl
var x
x = 10
```

A declaration at the top level of a program is a global. Functions can read and
write it. A declaration inside a block belongs to that block and is not visible
after the block ends.

```ccpl
var calls = 0

func bump()
    calls = calls + 1
    return calls
end

say(bump(), bump())
```

### Native types

Add a colon and a type to get an unboxed native C value.

```ccpl
var n: i32 = 42
var big: u64 = 9000000000
var pi: f64 = 3.14159
var ok: bool = true
var p: i8* = malloc(16)
var s: cstr = "hello"
```

The native types are `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`,
`f32`, `f64`, `bool`, `char`, `ptr`, and `cstr`. Append `*` for a pointer, and
repeat it for deeper pointers such as `u8**`. `ptr` is a generic `void *` and
`cstr` is a C string (`char *`).

A type may be written before or after the name:

```ccpl
var x: i32 = 1
var p: i32* = &x
var i32* q = &x
```

A dynamic value is converted when it reaches a native target. Numeric targets
use number conversion, and pointer and `cstr` targets use the string value:

```ccpl
var n: i32 = tonumber("42")
var s: cstr = "hello"
var q: i64 = (i64) 7
```

### Operators

From lowest to highest precedence:

* `or`
* `and`
* `==`, `~=`, `<`, `<=`, `>`, `>=`
* `..` (right associative)
* `+`, `-`
* `*`, `/`, `%`

Dynamic numbers are doubles, so `/` always yields a fraction and `%` is floor
modulo. Native integers use C rules for `/`, and `%` is floor modulo for both
sides. `-` negates, `not` is logical negation, `&` takes an address, `*`
dereferences, and `base[index]` indexes a pointer.

`and` and `or` short circuit and return one of their operands. Each operand is
evaluated once. `not` always returns a boolean.

### Control flow

```ccpl
var x = 0
var n = 3

if x < 0 then
    say("negative")
elseif x == 0 then
    say("zero")
else
    say("positive")
end

while n > 0 do
    n = n - 1
end

repeat
    n = n + 1
until n >= 10

for i = 0, 10 do
    say(i)
end

for i = 10, 0, -1 do
    say(i)
end
```

`for` includes the limit, and the optional third value is the step. A step of
zero is a runtime error. `break` and `continue` work inside loops.

### Functions

```ccpl
func add(a, b)
    return a + b
end

func mul(a: i32, b: i32): i32
    return a * b
end

func scale(p: f64*, k: f64): f64
    return *p * k
end
```

Functions are dynamically typed unless annotated. Native arguments are
converted automatically. Functions may be defined anywhere at the top level and
may call themselves or any other function. A function with no `return` yields
`nil`, or `0` for a native return type.

### Pointers and memory

```ccpl
var x: i32 = 42
var p: i32* = &x
*p = 99
say("now x =", x)

var buf: i8* = malloc(16)
buf[0] = 104
buf[1] = 105
say("bytes:", buf[0], buf[1])
free(buf)
```

`malloc`, `calloc`, `realloc`, and `free` are built in. Native values are
unboxed, so their address can be passed anywhere a pointer is expected. Keep
pointers in native variables. A pointer cannot be assigned to a dynamic
variable, though passing one to `say` prints its numeric address.

### Builtins

* `say(...)` prints values separated by tabs, then a newline.
* `hey(...)` prints values to standard error with a `hey:` prefix.
* `oh(msg)` prints `fatal: msg` to standard error and exits.
* `tonumber(x)` converts to a number, or returns `nil`.
* `tostring(x)` converts to a string.
* `malloc(n)`, `calloc(n, size)`, `realloc(p, n)`, and `free(p)` manage native
  memory.
* `pyexec(code)` runs Python statements.
* `pyeval(code)` evaluates a Python expression and converts the result.

### Importing code with `get`

`get` pulls another `.ccpl` file into the current program. It must appear at the
top level of a file.

```ccpl
-- get paths are relative to this file, so put this program at the repository root
get "examples/mathlib"
say(square(7))

get "examples/mathlib/cube"
say(cube(3))
```

Resolution order is:

1. Relative to the importing file.
2. `<compiler dir>\packages\`.
3. `<compiler dir>\..\packages\`.

A whole module or any single function may be imported, in any order. Each
definition is included at most once, and a module's top level statements run
once.

Top level declarations from imported modules become globals, so module
functions can use module state.

## Foreign function interface

> A foreign declaration runs real native code. Load only libraries and modules
> that you trust.

CCPL can call C and C++ functions from DLLs and can call into Python. A
declaration names the ABI, the signature, and the library or module.

```ccpl
extern "c" func strlen(s: cstr): u64 from "msvcrt.dll"
extern "python" func sqrt(x: f64): f64 from "math"

say(strlen("abcdef"))
say(sqrt(2.0))
```

The full guide, including C++ symbol names and Python conversion rules, is in
[docs/ffi.md](docs/ffi.md).

## Package manager

> Set `CLM_REGISTRY` to use a different registry. A `file://` URL is handy for
> local testing.

`clm` (Cool Library Manager) installs libraries for `get`. It is a pip like tool
backed by the [package registry](https://github.com/ccpl-lang/ccpl-packages).

```text
clm search <query>
clm install <name>[@<version>]
clm update [<name>]
clm remove <name>
clm list
clm login
clm whoami
clm logout
clm publish <dir> [--submit <url>]
```

Packages install into `<compiler dir>\packages\`, exactly where `get` looks.
The default registry is
`https://cdn.jsdelivr.net/gh/ccpl-lang/ccpl-packages@main`. Set `CLM_REGISTRY`
to override it; `file://` works for testing.

`clm publish` builds a zip archive for submission. With `--submit` and a logged
in account it uploads the archive to the given registry endpoint.

Full details are in [docs/clm.md](docs/clm.md).

## Examples

The `examples` folder contains runnable programs:

* `hello.ccpl` shows printing and variables.
* `controlflow.ccpl` shows loops, branches, and string concatenation.
* `features.ccpl` shows number formats, floor modulo, and logic.
* `fib.ccpl` shows recursion.
* `lowlevel.ccpl` shows native types, pointers, casts, and `malloc`.
* `getdemo.ccpl` with `mathlib.ccpl` shows modules.
* `bench.ccpl` shows tight loops and native speed output.
* `ffi_c.ccpl` calls C functions from `msvcrt.dll` and `kernel32.dll`.
* `ffi_python.ccpl` calls Python functions and evaluates Python code.
* `ffi_cpp` contains a C++ library and a program that calls it.

```bat
build\coolc.exe examples\bench.ccpl --run --fast
```

## Building from source

Requirements are Windows and TinyCC. TCC is small, so a release is often the
easiest route.

```bat
build.bat
```

Or call TCC directly:

```bat
tcc src\compiler.c -o build\coolc.exe
tcc src\clm.c -o build\clm.exe "%WINDIR%\System32\ws2_32.dll" "%WINDIR%\System32\shell32.dll"
copy src\runtime.h build\runtime.h
```

The compiler writes C to a temporary file, runs `tcc` on it, and deletes the
temporary file. Use `--show-c` to inspect the generated C.

## Testing

`tests\run.ps1` builds and runs every regression case, checks the expected
failure diagnostics, exercises the byte order mark path, and runs the foreign
function tests when the needed compilers are present.

```powershell
.\tests\run.ps1
```

The same script runs in CI on every push and pull request.

## Project layout

```text
ccpl/
  src/
    compiler.c      the compiler: lexer, parser, code generator, command line
    clm.c           the package manager
    runtime.h       runtime injected into every compiled program
    version.h       shared version define
  examples/         sample programs
  tests/            regression cases and the test runner
  docs/             language, FFI, package manager, and getting started guides
  build.bat         build script for the compiler and clm
  VERSION           version used by CI and the build
  LICENSE           GNU GPL v3
```

## Related repositories

* [ccpl](https://github.com/ccpl-lang/ccpl) is this compiler and language
  implementation, under GPL v3.
* [the toolchain repository](https://github.com/ccpl-lang/ccpl-toolchain) is a
  ready to run Windows bundle, under GPL v3.
* [the package registry](https://github.com/ccpl-lang/ccpl-packages) is the
  registry used by `clm`, under MIT.
* [the VS Code extension](https://github.com/ccpl-lang/vscode-ccpl) adds syntax
  highlighting, under MIT.

## License

CCPL is free software. You can redistribute it and modify it under the terms of
the GNU General Public License, version 3 or any later version. See `LICENSE`.

The bundled TinyCC toolchain is distributed under its own LGPL terms. See the
notices in the `ccpl-toolchain` repository.