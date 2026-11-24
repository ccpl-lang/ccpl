# CCPL documentation

Start here and pick the guide that matches what you want to do.

* [Getting started](getting-started.md): build a first program and run it.
* [Language reference](language.md): syntax, types, statements, and semantics.
* [Foreign function interface](ffi.md): call C, C++, and Python.
* [Package manager](clm.md): install, update, remove, and publish packages.

## Quick links

* [Compiler README](../README.md)
* [Regression tests](../tests/run.ps1)
* [Examples](../examples)
* [Changelog](../CHANGELOG.md)

## What is CCPL

CCPL is a small, Lua flavored language with optional native C types. It lowers
source to C and uses TinyCC to produce a native program, so there is no
interpreter and no virtual machine.

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