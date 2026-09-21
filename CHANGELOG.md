# Changelog

All notable changes to the CCPL compiler are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `clm` ("Cool Library Manager"), a pip-like package manager for CCPL:
  search, install (with dependency resolution), update, remove, list, and
  publish. Installs land in `<compiler-dir>\packages\` where `get` resolves
  them.
- `clm login` uses GitHub's device flow (no client_secret needed, like `gh
  auth login`): it prints a one-time code you enter at github.com/login/device.
  Tokens stay on the machine in `~\.clm\auth.json`.
- Registry support for the default
  `https://cdn.jsdelivr.net/gh/ccpl-lang/ccpl-packages@main` endpoint,
  overridable with `CLM_REGISTRY` (`file://` works for testing).
- `build.bat` and the release workflow now also build and ship `clm.exe`.

## [0.3.0] - 2026-09-21

### Added

- GitHub Actions build workflow that compiles `coolc.exe` and publishes a release
  on every build.
- `VERSION` file as the single source of truth for the compiler version.

## [0.2.0] - 2026-09-21

### Added

- Native parameter and return type annotations: `func f(a: i32): i32`.
- Automatic conversion between dynamic and native values in calls, assignments and
  native returns.
- `get "module/function"` to import a single function from a module.
- Module search under `<compiler-dir>\packages\` and `<compiler-dir>\..\packages\`.
- Name mangling for user functions so they cannot collide with C keywords or libc.
- `build.bat` to build the compiler with TinyCC.

### Fixed

- Compiler no longer crashes on calls with zero arguments (`oh()`, `malloc()`, user
  functions) or on functions that declare no parameters.
- Boolean literals, comparisons and `not` now produce real booleans, so `say(true)`
  prints `true`.
- Numbers print in full precision instead of `%g` scientific notation.
- `%` is consistently floor-mod for both dynamic and native operands.
- `free()` is documented to take a pointer and is validated.
- Duplicate `free` handling removed; embedded NUL bytes in string literals are
  rejected with a clear error.
- Generated C is written to the system temp directory instead of the install dir.
- Undefined functions now produce `undefined function 'x'` instead of a TinyCC error.

## [0.1.0] - 2026-09-01

### Added

- Initial release: lexer, parser, C code generator and CLI.
- Dynamic values plus native integer, float, boolean, char and pointer types.
- `say`/`hey`/`oh`, `tonumber`/`tostring`, `malloc`/`calloc`/`realloc`/`free`.
- `get` module imports.
