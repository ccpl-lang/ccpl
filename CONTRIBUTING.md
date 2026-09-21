# Contributing to CCPL

Thanks for your interest in CCPL! This document explains how to build, test and
submit changes.

## Code of conduct

By participating you agree to abide by the [Code of Conduct](CODE_OF_CONDUCT.md).

## Building

You need Windows and [TinyCC](https://bellard.org/tcc/).

```bat
build.bat
```

`build.bat` looks for the TCC driver at `%TCC%`, then `toolchain\tcc\tcc.exe`, then
`tcc` on `PATH`. You can also build directly:

```bat
tcc src\compiler.c -o build\coolc.exe
copy src\runtime.h build\runtime.h
```

## Testing

There is no test framework yet; the examples double as smoke tests. Build and run
every example and check the exit codes:

```bat
for %f in (examples\*.ccpl) do build\coolc.exe %f --run
```

When you change code generation, `--show-c` is your friend:

```bat
build\coolc.exe examples\hello.ccpl --show-c
```

## Guidelines

- Keep the compiler a single self-contained C file (`src/compiler.c`) unless there is
  a strong reason to split it.
- Match the existing code style: 4-space indentation, `snake_case` for functions,
  small helpers, no external dependencies.
- Compile cleanly with warnings on (`tcc -Wall`).
- Update `CHANGELOG.md` under *Unreleased* for user-visible changes.
- Keep `src/runtime.h` and the copy shipped in `ccpl-toolchain` in sync.

## Submitting a pull request

1. Open an issue first for anything larger than a bug fix, so we can agree on the
   approach.
2. Fork the repository and create a topic branch.
3. Make your change, build it, and run the examples.
4. Describe what you changed and why in the pull request, and reference any issues.

## Reporting bugs

Please include:

- the CCPL program (or a minimal reproduction),
- the command you ran,
- what you expected and what happened,
- your Windows version and the output of `coolc --version`.

## License

By contributing you agree that your contributions are licensed under the
[GNU GPL v3](LICENSE).
