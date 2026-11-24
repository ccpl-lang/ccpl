# Getting started with CCPL

This guide takes you from nothing to a running native program in a couple of
minutes.

> You need Windows x64. TinyCC comes with the toolchain bundle. Python and a C++
> compiler are optional and only needed for those foreign function features.

## Contents

* [1. Get the compiler](#1-get-the-compiler)
* [2. Write a program](#2-write-a-program)
* [3. Compile and run](#3-compile-and-run)
* [4. Look at the generated C](#4-look-at-the-generated-c)
* [5. Try native code](#5-try-native-code)
* [6. Call a foreign function](#6-call-a-foreign-function)
* [7. Install a package](#7-install-a-package)
* [Next steps](#next-steps)

## 1. Get the compiler

The fastest path on Windows is the ready to run toolchain bundle, which contains
the compiler, `clm`, the runtime, and TinyCC in one folder:

```bat
git clone https://github.com/ccpl-lang/ccpl-toolchain.git
cd ccpl-toolchain
```

To build from source instead, clone the compiler repository and run `build.bat`.
See the main [README](../README.md) for the build order and requirements.

## 2. Write a program

Create `hello.ccpl`:

```ccpl
say("hello, world")

var name = "CCPL"
var year = 2026
say("welcome to", name, "in", year)
```

## 3. Compile and run

```bat
coolc.bat hello.ccpl --run
```

You should see:

```text
hello, world
welcome to    CCPL    in    2026
```

`coolc` writes `hello.exe` next to the source. Run the program again without
`--run` to only rebuild. The result is a normal native program with no
dependency on the compiler.

## 4. Look at the generated C

Ask the compiler to show its output:

```bat
coolc.bat hello.ccpl --show-c
```

Every CCPL program is lowered to plain C and compiled by TinyCC. There is no
interpreter involved.

## 5. Try native code

```ccpl
var x: i32 = 42
var p: i32* = &x
*p = 99
say("x is now", x)
```

Top level variables are globals, so functions can use them:

```ccpl
var counter = 0

func next()
    counter = counter + 1
    return counter
end

say(next(), next())
```

## 6. Call a foreign function

```ccpl
extern "c" func strlen(s: cstr): u64 from "msvcrt.dll"

say("length:", strlen("hello"))
```

Python and C++ work in a similar way. See [ffi.md](ffi.md) for the full guide.

## 7. Install a package

Find a package and install it:

```bat
clm search math
clm install <name>
```

Then import it by name:

```text
get "<name>"
```

The installed files go to `packages\` next to the compiler, which is where `get`
looks. See [clm.md](clm.md) for registry settings, version ranges, and
publishing.

## Next steps

* Read the [language reference](language.md) for the full syntax.
* Browse the `examples` folder for complete programs.
* Run the regression suite with `tests\run.ps1` after changing the compiler.

Happy hacking.