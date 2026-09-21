# Getting started with CCPL

This guide takes you from nothing to a running native executable in a couple of minutes.

## 1. Get the compiler

The fastest path on Windows is the ready-to-run toolchain bundle, which ships the
compiler, its runtime and TinyCC in one folder:

```bat
git clone https://github.com/ccpl-lang/ccpl-toolchain.git
cd ccpl-toolchain
```

If you prefer to build from source, clone the compiler repository and run `build.bat`
(see the [main README](../README.md#building-from-source)).

## 2. Write a program

Create `hello.ccpl`:

```ccpl
-- hello.ccpl
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

```
hello, world
welcome to    CCPL   in   2026
```

`coolc` writes `hello.exe` next to the source. Run it again without `--run` to just
rebuild, or execute the `.exe` directly â€” it has no dependencies on the compiler.

## 4. Look at the generated C

Curious how it works? Ask the compiler to show its output:

```bat
coolc.bat hello.ccpl --show-c
```

Every CCPL program is lowered to plain C and compiled by TinyCC. There is no
interpreter involved.

## 5. Next steps

- Skim the [language reference](language.md) for the full syntax.
- Browse [`examples/`](../examples) for complete programs.
- Try native code:

  ```ccpl
  var x: i32 = 42
  var p: i32* = &x
  *p = 99
  say("x is now", x)
  ```

Happy hacking!
