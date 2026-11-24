# Changelog

All notable changes to CCPL are documented here. The format follows Keep a
Changelog, and versions follow Semantic Versioning.

## [0.4.0] on November 24, 2026

### Added

* Foreign function interface for C and C++ DLLs, with lazy symbol loading,
  cached function pointers, C string conversion, and explicit symbol names for
  decorated C++ functions. See `docs/ffi.md`.
* Python interoperability through `extern "python"` declarations, plus the
  `pyexec` and `pyeval` builtins. The runtime discovers a versioned Python DLL
  and falls back to the stable ABI DLL.
* `cstr` native type.
* Regression test suite in `tests`, covering language behavior, diagnostics,
  the UTF 8 byte order mark, C and C++ foreign calls, Python calls, and the
  package manager.
* Documentation for the language, foreign functions, the package manager, and
  the test workflow.

### Fixed

* Dynamic strings no longer corrupt older values after 1024 allocations. Strings
  are now allocated for the process lifetime.
* Variables declared in `if`, `while`, and `repeat` blocks no longer leak into
  later code.
* Top level variables are real globals, so functions and module functions can
  read and write module state.
* `and` and `or` evaluate their left operand once.
* `ptr` works as a generic pointer type.
* Multi level pointers work with dereference and indexing.
* Native declarations accept dynamic initializers, and casts convert dynamic
  values.
* Importing several functions from one module works in any order, and each
  definition is included once.
* Source files with a UTF 8 byte order mark compile.
* A top level `return` is a clear compiler error instead of a C error.
* `realloc` passes its pointer argument correctly.
* `for` loops reject a zero step at runtime.
* Compiler and package manager now share one version define from
  `src/version.h`, matching `VERSION`.
* The release workflow only publishes on version tags.
* `build.bat` finds TinyCC in `build\tcc` and in a sibling toolchain checkout.
* `clm` no longer builds shell command lines from package names, validates
  names, verifies archive paths, downloads before replacing an installed
  package, compares versions numerically, uses a strong random source for OAuth
  values, requires the callback state, refreshes expired tokens, and reports
  dependency failures.
* `clm publish` validates the manifest, stages files without `cmd.exe`, supports
  `--submit`, and leaves the archive in place on failure.

### Changed

* `hey` and `oh` prefixes are documented.
* Error messages for undefined variables and functions are now produced by the
  compiler rather than by TinyCC.

## [0.3.0] on September 21, 2026

### Added

* GitHub Actions build workflow that compiles the compiler and publishes a
  release.
* `VERSION` file as the version source for the build.

## [0.2.0] on September 21, 2026

### Added

* Native parameter and return type annotations.
* Automatic conversion between dynamic and native values in calls, assignments,
  and native returns.
* `get "module/function"` for importing one function.
* Module search under the compiler `packages` directories.
* Name mangling for user functions.
* `build.bat`.

### Fixed

* Calls with zero arguments no longer crash.
* Boolean literals, comparisons, and `not` produce real booleans.
* Numbers print in full precision.
* Percent is floor modulo for dynamic and native operands.
* `free` takes a pointer and is validated.
* Embedded NUL bytes in strings are rejected.
* Generated C goes to the system temp directory.
* Undefined functions produce a compiler error.

## [0.1.0] on September 1, 2026

### Added

* Initial release: lexer, parser, C code generator, and command line.
* Dynamic values plus native integer, float, boolean, char, and pointer types.
* `say`, `hey`, `oh`, `tonumber`, `tostring`, `malloc`, `calloc`, `realloc`, and
  `free`.
* `get` module imports.
