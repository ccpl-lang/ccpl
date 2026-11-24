# Contributing to CCPL

Thanks for your interest in CCPL. This document explains how to build, test, and
submit changes.

## Code of conduct

By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Building

You need Windows and TinyCC.

```bat
build.bat
```

`build.bat` looks for the TinyCC driver at `%TCC%`, then `build\tcc\tcc.exe`,
then `..\ccpl-toolchain\tcc\tcc.exe`, then `tcc` on `PATH`. You can also build
directly:

```bat
tcc src\compiler.c -o build\coolc.exe
tcc src\clm.c -o build\clm.exe "%WINDIR%\System32\ws2_32.dll" "%WINDIR%\System32\shell32.dll"
copy src\runtime.h build\runtime.h
```

## Testing

There is a regression suite:

```powershell
.\tests\run.ps1
```

It covers the language, error messages, the UTF 8 byte order mark, C and C++
foreign calls, Python calls when Python is available, and the package manager.
The C++ test builds `examples/ffi_cpp/ffitest.cpp` with the Visual C++ tools when
they are installed, and skips that case otherwise.

When you change code generation, `--show-c` is the best debugging tool:

```bat
build\coolc.exe examples\hello.ccpl --show-c
```

## Guidelines

* Keep the compiler in one C file (`src/compiler.c`) unless there is a strong
  reason to split it.
* Match the existing style: four space indentation, `snake_case` for functions,
  small helpers, and no external dependencies.
* Compile cleanly with warnings on (`tcc -Wall`).
* Update `CHANGELOG.md` under the next version for user visible changes.
* Keep `src/runtime.h` and the copy in `ccpl-toolchain` byte for byte identical.
* Keep `VERSION` and `src/version.h` in sync.
* Add a case under `tests` when you fix a bug.

## Submitting a pull request

1. Open an issue first for anything larger than a bug fix, so we can agree on
   the approach.
2. Fork the repository and create a topic branch.
3. Make your change, build it, and run the tests.
4. Describe what changed and why in the pull request, and reference any issues.

## Reporting bugs

Please include:

* the program or a minimal reproduction,
* the command you ran,
* what you expected and what happened,
* your Windows version and the output of `coolc --version`.

## License

By contributing you agree that your contributions are licensed under the GNU
General Public License, version 3.