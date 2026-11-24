# Foreign function interface

CCPL can call functions that live outside the language. There are three ABIs:

* `"c"` loads a symbol from a DLL and calls it with the C calling convention.
* `"c++"` uses the same loader, but is meant for C++ symbols. Native C++ name
  decoration is not generated, so pass the decorated symbol with `as` unless
  the function was declared `extern "C"`.
* `"python"` imports a Python module, looks up an attribute, and calls it with
  converted arguments.

All foreign declarations are resolved when the function is first called, not
when the program starts. A missing DLL or symbol stops the program with a clear
message.

> A declaration runs native code with the full permissions of your program.
> Load only libraries and modules that you trust.

## Contents

* [Syntax](#syntax)
* [Type mapping](#type-mapping)
* [Calling C](#calling-c)
* [Calling C++](#calling-c-1)
* [Calling Python](#calling-python)
* [Return values and lifetimes](#return-values-and-lifetimes)
* [Errors](#errors)
* [Limits](#limits)
* [Examples](#examples)

## Syntax

```text
extern "<abi>" func <name>(<params>) [: <return type>] from "<library>" [as "<symbol>"]
```

Examples:

```ccpl
extern "c" func strlen(s: cstr): u64 from "msvcrt.dll"
extern "c" func GetTickCount(): u32 from "kernel32.dll"
extern "c++" func vec_len(x: f64, y: f64): f64 from "ffitest.dll" as "?vec_len@@YANNN@Z"
extern "python" func sqrt(x: f64): f64 from "math"
extern "python" func getcwd(): cstr from "os"
```

The declaration may appear anywhere a statement is allowed, and the function is
visible to the whole program. A foreign name may not reuse a builtin name.

## Type mapping

C and C++ declarations use the native types that CCPL already has.

* `i8`, `i16`, `i32`, `i64` map to the matching signed C integer type.
* `u8`, `u16`, `u32`, `u64` map to the matching unsigned C integer type.
* `f32` and `f64` map to `float` and `double`.
* `bool` maps to C `bool`.
* `char` maps to C `char`.
* `ptr` maps to `void *`.
* `cstr` maps to `char *`.
* A trailing `*` adds another pointer level.

C and C++ parameters must have native types. A dynamic argument is converted at
the call site. Numbers use `cvtod`, and a string passed to a pointer or `cstr`
parameter is passed as a C string.

A C declaration without a return type is treated as returning `void`. Use the
call as a statement in that case.

## Calling C

```ccpl
extern "c" func strlen(s: cstr): u64 from "msvcrt.dll"
extern "c" func pow(x: f64, y: f64): f64 from "msvcrt.dll"
extern "c" func MessageBoxA(hwnd: ptr, text: cstr, caption: cstr, kind: u32): i32 from "user32.dll"

say("length:", strlen("abcdef"))
say("two to the tenth:", pow(2.0, 10.0))
MessageBoxA(0, "hello", "ccpl", 0)
```

The library string is passed to `LoadLibraryA`. Windows search rules apply:
the program directory first, then the system directories, then `PATH`.

The optional `as` clause uses a different symbol name:

```ccpl
extern "c" func getpid(): i32 from "msvcrt.dll" as "_getpid"
```

## Calling C++

C++ compilers decorate function names. CCPL does not decode or generate those
names, so use `as` with the exact exported symbol. A free function and a non
static member function both work as long as the target is built for x64.

```ccpl
extern "c++" func vec_len(x: f64, y: f64): f64 from "ffitest.dll" as "?vec_len@@YANNN@Z"
extern "c++" func vec_scaled(self: ptr, k: f64): f64 from "ffitest.dll" as "?scaled@Vec2@@QEBANN@Z"

var v: f64* = malloc(16)
v[0] = 3.0
v[1] = 4.0
say(vec_len(3.0, 4.0))
say(vec_scaled(v, 2.0))
free(v)
```

Build `ffitest.dll` from `examples/ffi_cpp` before running this, or use the same
pattern with your own library.

A member function receives its object pointer as the first argument, followed by
the declared parameters. This matches the x64 calling convention for a simple
non virtual member function.

What is supported:

* Free functions with C++ linkage and an explicit symbol name.
* Static member functions.
* Non virtual member functions when the object pointer is passed explicitly.
* Classes whose layout you build yourself with native pointers and `malloc`.

What is not supported:

* Automatic symbol decoration.
* Virtual dispatch, constructors, destructors, exceptions, and RTTI.
* Passing C++ objects by value.
* 32 bit `__thiscall` details. CCPL targets x64.

The safest C++ interface is still a small `extern "C"` bridge inside the C++
library. That gives a stable symbol name and avoids ABI details.

## Calling Python

A Python declaration names a module and an attribute:

```ccpl
extern "python" func sqrt(x: f64): f64 from "math"
extern "python" func gcd(a: i32, b: i32): i32 from "math"
extern "python" func getcwd(): cstr from "os"
extern "python" func join(a: cstr, b: cstr): cstr from "os.path" as "join"
```

Parameters always arrive in Python as dynamic values. A parameter annotation on
a Python declaration is documentation; the value is converted from the CCPL
value that the caller supplied. A return annotation controls the conversion
back into CCPL. Without a return annotation the result stays dynamic:

```ccpl
extern "python" func asin(x: f64) from "math"
say(asin(1.0))
```

Conversion rules for Python arguments:

* `nil` becomes `None`.
* `true` and `false` become integers `1` and `0`.
* An integral number becomes a Python `int` when it fits in 53 bits.
* Any other number becomes a Python `float`.
* A string becomes a Python `str`.

When the result comes back:

* `None` becomes `nil`.
* A Python `bool` becomes a CCPL boolean.
* A Python `int` or `float` becomes a number. Integers larger than 53 bits lose
  precision because CCPL numbers are doubles.
* A Python `str` is copied and becomes a CCPL string.
* Any other object becomes the string `<python object>`.

Two builtins cover quick cases without a declaration:

```ccpl
pyexec("import math")
say(pyeval("math.factorial(6)"))
```

`pyexec` runs statements and returns `nil`. `pyeval` evaluates one expression
and converts the result.

### Finding Python

The runtime looks for a Python DLL in this order:

1. The `CCPL_PYTHON_DLL` environment variable, which may contain a full path.
2. A versioned `python3NN.dll` under common install roots, including
   `%LOCALAPPDATA%\Programs\Python` and `%ProgramFiles%`.
3. `python3.dll` on the normal library search path. If that is the stable ABI
   DLL and lacks a needed symbol, the runtime reads the Python version and tries
   the matching versioned DLL next to it.

A Python installed in a custom location can always be used by setting
`CCPL_PYTHON_DLL`.

The Python runtime stays alive for the whole program. Calls share the `__main__`
module, so `pyexec` and later `pyeval` or foreign calls see the same state.

## Return values and lifetimes

Numbers, booleans, and pointers come back as native values. A `cstr` return
becomes a CCPL string that points at the memory the C function returned. That
memory is not copied. It must stay valid while the program uses it. A string
returned from Python is copied and owned by the CCPL runtime.

Strings produced by CCPL are immutable and live until the process exits. There
is no garbage collector.

## Errors

* A library that cannot be loaded stops the program with the library name.
* A symbol that cannot be found stops the program with the symbol and library.
* A Python import, attribute lookup, or call that fails prints the Python
  traceback and exits with status 1.
* Calling a C function with a wrong signature or the wrong calling convention is
  undefined behavior. The declaration is trusted.

## Limits

* Variadic C functions such as `printf` are not supported, because CCPL cannot
  infer the types of the extra arguments.
* Calling conventions other than the default x64 convention are not modeled.
* C++ symbol decoration is not generated.
* There are no structs, unions, or arrays as language types. Use pointers,
  offsets, and `malloc` when a library needs a structure.
* Only the Windows platform is supported.

## Examples

The `examples` folder has working programs:

* `examples/ffi_c.ccpl` calls `puts`, `strlen`, `pow`, and `GetTickCount`.
* `examples/ffi_python.ccpl` calls `math.sqrt`, `math.gcd`, and `os.getcwd`,
  then evaluates `math.factorial`.
* `examples/ffi_cpp` contains `ffitest.cpp` and `use_ffi.ccpl`. Build the DLL
  with a C++ compiler, then run the CCPL program from that folder.