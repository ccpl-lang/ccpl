# CCPL

**Cool Compilable Programming Language** â€” a small, Lua-flavored language that compiles straight to a native executable. No interpreter, no bytecode, no VM.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.3.0-informational.svg)](CHANGELOG.md)
[![Platform](https://img.shields.io/badge/platform-Windows-0078D6.svg)](#requirements)
[![Made with C](https://img.shields.io/badge/made%20with-C-555.svg)](#building-from-source)
[![CI](https://github.com/ccpl-lang/ccpl/actions/workflows/ci.yml/badge.svg)](.github/workflows/ci.yml)

CCPL mixes a dynamic, Lua-like core with escape hatches to native C types, pointers and `malloc`, then lowers everything to C which [TinyCC](https://bellard.org/tcc/) turns into a native `.exe` in milliseconds.

```ccpl
-- hello world in CCPL
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

## Table of contents

- [Why CCPL](#why-ccpl)
- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Command line](#command-line)
- [clm — the package manager](#clm--the-package-manager)
- [Language tour](#language-tour)
  - [Comments](#comments)
  - [Values and variables](#values-and-variables)
  - [Native types](#native-types)
  - [Operators](#operators)
  - [Control flow](#control-flow)
  - [Functions](#functions)
  - [Pointers and memory](#pointers-and-memory)
  - [Builtins](#builtins)
  - [Importing code with `get`](#importing-code-with-get)
- [Examples](#examples)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [Related repositories](#related-repositories)
- [License](#license)

## Why CCPL

- **Truly compiled.** Source lowers to C, TCC emits a native binary. There is no runtime interpreter.
- **Tiny and fast to build.** A whole program compiles in a few milliseconds.
- **Dynamic by default.** Numbers are doubles, plus strings, booleans and `nil`, with Lua-style `and`/`or`/`not` and `..` concatenation.
- **Native when you need it.** `i8`â€¦`i64`, `u8`â€¦`u64`, `f32`/`f64`, `bool`, `char` and `ptr`, plus `&`/`*`, indexing and `malloc`/`free`.
- **One self-contained C file.** The compiler is a single ~2k-line C source with no dependencies beyond the Windows API and TCC.

## Requirements

- Windows (the compiler currently uses `windows.h` for process spawning and path handling).
- [TinyCC](https://bellard.org/tcc/) â€” either the bundled toolchain or a local install. The easiest route is the ready-to-run [`ccpl-toolchain`](https://github.com/ccpl-lang/ccpl-toolchain) bundle.

## Quick start

### Option A â€” ready-to-run toolchain

Download or clone [`ccpl-toolchain`](https://github.com/ccpl-lang/ccpl-toolchain), then:

```bat
coolc.bat hello.ccpl --run
```

### Option B â€” build the compiler yourself

```bat
git clone https://github.com/ccpl-lang/ccpl.git
cd ccpl
build.bat
build\coolc.exe examples\hello.ccpl --run
```

`build.bat` looks for the TinyCC driver in this order: `%TCC%`, `toolchain\tcc\tcc.exe`, then `tcc` on `PATH`.

## Command line

```
coolc <source.ccpl> [-o out.exe] [--run] [--fast] [--show-c]
```

| Flag | Meaning |
| --- | --- |
| `-o, --output <file>` | Output executable path (default: source name with `.exe`). |
| `--run` | Run the executable after a successful build. |
| `--fast` | Compile the generated C with `-O2 -s`. |
| `--show-c` | Print the generated C and exit (great for debugging). |
| `--version, -v` | Print the compiler version. |
| `-h, --help` | Show usage. |

## clm — the package manager

`clm` ("Cool Library Manager") ships with the compiler and installs libraries for `get`. It is a pip-like tool backed by the [`ccpl-packages`](https://github.com/ccpl-lang/ccpl-packages) registry.

```
clm search <query>          search the registry
clm install <name>[@<ver>]  install a package (and its dependencies)
clm update [<name>]         update installed package(s)
clm remove <name>           uninstall a package
clm list                    list installed packages
clm login                   link this machine to a GitHub account (for publishing)
clm whoami                  show the linked account
clm logout                  forget the linked account
clm publish <dir>           build a library for publishing
```

Packages install into `<compiler-dir>\packages\`, exactly where `get` looks. The default registry is `https://cdn.jsdelivr.net/gh/ccpl-lang/ccpl-packages@main`; override it with the `CLM_REGISTRY` environment variable (`file://` works for testing).

## Language tour

### Comments

```ccpl
-- a line comment
--[[ a
     block comment ]]
```

### Values and variables

Dynamic values are numbers (doubles), strings, booleans and `nil`.

```ccpl
var greeting = "hello"
variable count = 3
var flag = true
var nothing = nil

count = count + 1
say(greeting, count, flag, nothing)   --> hello   4   true   nil
```

Declare a variable without a value to get `nil`:

```ccpl
var x
x = 10
```

### Native types

Add a type after `:` to get a native C value. Native variables are unboxed and use C semantics.

```ccpl
var n: i32 = 42
var big: u64 = 9000000000
var pi: f64 = 3.14159
var ok: bool = true
var p: i8* = malloc(16)
```

| Category | Types |
| --- | --- |
| Signed integers | `i8`, `i16`, `i32`, `i64` |
| Unsigned integers | `u8`, `u16`, `u32`, `u64` |
| Floating point | `f32`, `f64` |
| Other | `bool`, `char`, `ptr` |

Append `*` for pointers (`i32*`, `u8**`, â€¦). `ptr` is a generic `void *`.

### Operators

From lowest to highest precedence:

| Operators | Notes |
| --- | --- |
| `or` | Short-circuits, returns an operand |
| `and` | Short-circuits, returns an operand |
| `==` `~=` `<` `<=` `>` `>=` | Comparisons |
| `..` | String concatenation (right associative) |
| `+` `-` | Add, subtract |
| `*` `/` `%` | Multiply, divide, modulo |

- Dynamic numbers are doubles, so `/` always yields a fraction and `%` is floor-mod (`-7 % 3` is `2`).
- Native integers keep C semantics for `/` (truncating) but `%` is also floor-mod.
- `-` negates, `not` is logical negation, `#` is not used. Cast with `(type)expr`, dereference with `*`, address-of with `&`, index with `base[i]`.

### Control flow

```ccpl
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

for i = 0, 10 do          -- inclusive, optional step
    say(i)
end

for i = 10, 0, -1 do      -- count down
    say(i)
end
```

`break` and `continue` work inside loops.

### Functions

```ccpl
func add(a, b)
    return a + b
end

-- native parameters and return type
func mul(a: i32, b: i32): i32
    return a * b
end

-- pointers work too
func scale(p: f64*, k: f64): f64
    return *p * k
end
```

Functions are dynamically typed unless annotated. Native arguments are converted automatically when needed.

### Pointers and memory

```ccpl
var x: i32 = 42
var p: i32* = &x
*p = 99
say("now x =", x)         --> now x = 99

var buf: i8* = malloc(16)
buf[0] = 104
buf[1] = 105
say("bytes:", buf[0], buf[1])
free(buf)
```

`malloc`, `calloc`, `realloc` and `free` are built in. Because native values are unboxed, you can take their address and pass them anywhere a pointer is expected.

### Builtins

| Builtin | Description |
| --- | --- |
| `say(...)` | Print values separated by tabs, then a newline. |
| `hey(...)` | Print values to stderr. |
| `oh(msg)` | Print `msg` to stderr and abort. |
| `tonumber(x)` | Convert to a number, or `nil` if it cannot. |
| `tostring(x)` | Convert to a string. |
| `malloc(n)` / `calloc(n, size)` / `realloc(p, n)` / `free(p)` | Native memory helpers. |

### Importing code with `get`

`get` pulls another `.ccpl` file into the current program.

```ccpl
get "mathlib"            -- imports the whole file

say(square(7))
```

Resolution order:

1. relative to the importing file,
2. `<compiler-dir>\packages\`,
3. `<compiler-dir>\..\packages\`.

You can also import a single function:

```ccpl
get "mathlib/square"
```

Each module is loaded at most once, so diamond imports are safe.

## Examples

The [`examples/`](examples) folder contains runnable programs:

| File | Shows off |
| --- | --- |
| [`hello.ccpl`](examples/hello.ccpl) | Printing and variables |
| [`controlflow.ccpl`](examples/controlflow.ccpl) | Loops, `if`/`else`, string concat |
| [`features.ccpl`](examples/features.ccpl) | Hex/float literals, floor-mod, `and`/`or`/`not` |
| [`fib.ccpl`](examples/fib.ccpl) | Recursion |
| [`lowlevel.ccpl`](examples/lowlevel.ccpl) | Native types, pointers, casts, `malloc` |
| [`getdemo.ccpl`](examples/getdemo.ccpl) + [`mathlib.ccpl`](examples/mathlib.ccpl) | Modules with `get` |
| [`bench.ccpl`](examples/bench.ccpl) | Tight loops and native-speed output |

```bat
coolc examples\bench.ccpl --run --fast
```

## Building from source

Requirements: Windows and [TinyCC](https://bellard.org/tcc/). TCC is tiny (~1 MB), so it is often easier to grab a release.

```bat
build.bat
```

Or invoke TCC directly:

```bat
tcc src\compiler.c -o build\coolc.exe
copy src\runtime.h build\runtime.h
```

The compiler emits C to a temporary file, runs `tcc` on it, and deletes it. Use `--show-c` to inspect the generated C.

## Project layout

```
ccpl/
â”œâ”€â”€ src/
â”‚   â”œâ”€â”€ compiler.c      # the whole compiler: lexer, parser, codegen, CLI
â”‚   â”œâ”€â”€ clm.c           # the clm package manager
â”‚   â””â”€â”€ runtime.h       # runtime helpers injected into every program
â”œâ”€â”€ examples/           # sample .ccpl programs
â”œâ”€â”€ docs/               # language reference and guides
â”œâ”€â”€ build.bat           # build script for the compiler and clm
â””â”€â”€ LICENSE             # GNU GPL v3
```

## Related repositories

| Project | Description | License |
| --- | --- | --- |
| **ccpl** | Compiler and language implementation (this repo) | GPL-3.0 |
| **ccpl-toolchain** | Ready-to-run Windows toolchain bundle | GPL-3.0 |
| **ccpl-packages** | The `clm` package registry | MIT |
| **vscode-ccpl** | VS Code syntax highlighting | MIT |

## License

CCPL is free software: you can redistribute it and/or modify it under the terms of the
**GNU General Public License** as published by the Free Software Foundation, either
version 3 of the License, or (at your option) any later version. See [LICENSE](LICENSE).

The bundled [TinyCC](https://bellard.org/tcc/) toolchain is distributed under its own
(LGPL) terms â€” see the notices in the `ccpl-toolchain` repository.
