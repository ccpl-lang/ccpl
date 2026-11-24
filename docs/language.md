# The CCPL language reference

CCPL is a small dynamically typed language with optional native C types. This
document describes the syntax and semantics implemented by the compiler in
`src/compiler.c`.

## Contents

* [Lexical structure](#lexical-structure)
* [Types and values](#types-and-values)
* [Conversions](#conversions)
* [Statements](#statements)
* [Expressions](#expressions)
* [Functions](#functions)
* [Globals and blocks](#globals-and-blocks)
* [Modules with `get`](#modules-with-get)
* [Builtins](#builtins)
* [Foreign functions](#foreign-functions)
* [Full grammar](#full-grammar)

> Native memory is not managed. Free what you allocate, and keep pointers in
> native variables when they must be exact.

## Lexical structure

### Whitespace and comments

Spaces, tabs, and carriage returns are ignored. A newline ends a statement, and
a semicolon also separates statements on the same line. An expression may not
continue on a new line.

```ccpl
-- a line comment runs to the end of the line
--[[
    a block comment
    can span many lines
]]
```

### Identifiers and keywords

Identifiers start with a letter or underscore and continue with letters, digits,
or underscores. These words are reserved:

```text
var variable func end if then elseif else while do repeat until for
return break continue get and or not true false nil
i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char ptr cstr
extern from as
```

### Numbers

* Decimal integers: `42`, `-7`
* Floating point: `3.14`, `.5`, `2.5e-2`, `1e3`
* Hexadecimal: `0xFF`, `0x10`

Numbers are doubles at runtime, even when written as integers. Integer literals
keep enough type information to generate a C integer constant, which matters for
large native values.

### Strings

Strings use double quotes and support these escapes:

* `\n`, `\t`, `\r`, `\a`, `\b`, `\f`, `\v` for control characters
* `\\` for a backslash
* `\"` for a double quote
* `\ddd` for a decimal byte value, with one to three digits

A string may not contain a NUL byte. `\0` is rejected.

A UTF 8 byte order mark at the start of a source file is skipped.

### Operators

```text
( ) [ ] { } , ; = == ~= < <= > >= + - * / % .. & : .
```

The braces and the single dot are reserved for future use and are not accepted
by the current parser. The colon is used for type annotations.

## Types and values

A value is either dynamic or native.

### Dynamic values

Dynamic values are boxed and behave like Lua values:

* Number: always a double.
* String: immutable text.
* Boolean: `true` or `false`.
* Nil: the absence of a value.

`nil` and `false` are falsy. Everything else is truthy, including the number
zero.

### Native values

Native values are plain C values with a declared type and use C semantics.

```ccpl
var n: i32 = 42
var ratio: f64 = 0.5
var ok: bool = true
var p: i8* = malloc(16)
var name: cstr = "ccpl"
```

The mapping is:

* `i8`, `i16`, `i32`, `i64` to `int8_t`, `int16_t`, `int32_t`, `int64_t`
* `u8`, `u16`, `u32`, `u64` to `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`
* `f32`, `f64` to `float`, `double`
* `bool` to `bool`
* `char` to `char`
* `ptr` to `void *`
* `cstr` to `char *`

A trailing `*` makes a pointer, and it may be repeated (`u8**`). The type may be
written before or after the name:

```ccpl
var x: i32 = 1
var p: i32* = &x
var i32* q = &x
```

A pointer target may be any depth that the compiler can track, up to eight
levels. Dereference and indexing reduce the depth by one. Reading or writing the
last level yields a native number.

## Conversions

A dynamic value becomes native when it reaches a native target:

* Numeric targets use number conversion, the same rules as `tonumber`.
* Pointer and `cstr` targets use the string value.
* A native value assigned to a dynamic variable is boxed. Numbers and booleans
  become numbers or booleans. A `cstr` becomes a dynamic string. Other pointers
  cannot be stored in a dynamic variable; they may be printed as numbers, which
  loses precision on x64.

Casts convert explicitly:

```ccpl
var q: i8* = malloc(8)
var value = "hello"
var n: i64 = (i64) 42
var p: i8* = (i8*) q
var text: cstr = (cstr) value
```

A cast from a dynamic value is allowed. A pointer cast to a numeric type uses
the integer value of the pointer.

## Statements

### Declarations

```ccpl
var a
var b = 1
variable c = "text"
var d: f64 = 2.5
```

A declaration without a value starts as `nil` for a dynamic value or zero for a
native value. A declaration at the top level is a global. A declaration inside
a block belongs to that block.

### Assignment

```ccpl
var x = 0
var i: i32 = 0
var arr: i32* = malloc(8)
var p: i32* = malloc(8)
x = 10
arr[i] = 3
*p = 99
```

Assigning a dynamic value to native memory converts it. Assigning a string to a
pointer target passes the C string.

### Conditionals

```ccpl
var x = 0
if x < 0 then
    say("negative")
elseif x == 0 then
    say("zero")
else
    say("positive")
end
```

### Loops

```ccpl
var n = 3
while n > 0 do
    n = n - 1
end

repeat
    n = n + 1
until n >= 10

for i = 0, 10 do
    say(i)
end
```

`for` includes `last`. The step may be negative to count down, and a step of
zero is a runtime error. `break` leaves the innermost loop. `continue` skips to
the next iteration.

The loop variable and any declarations in the body belong to the loop block.

### Return

```ccpl
func sign(n)
    if n < 0 then
        return -1
    end
    return n
end
```

A bare `return` also works and yields `nil`, or `0` for a native return type.
A `return` outside a function is a compile error.

## Expressions

Operator precedence, lowest to highest:

1. `or`
2. `and`
3. `==`, `~=`, `<`, `<=`, `>`, `>=`
4. `..`, right associative
5. `+`, `-`
6. `*`, `/`, `%`

### Arithmetic

* Dynamic operands are doubles. Division yields a fraction and modulo is floor
  modulo.
* Native integer operands follow C for division. Modulo is floor modulo for both
  dynamic and native operands.
* Mixing a dynamic and a native operand promotes the expression to dynamic.

### Comparison and equality

* Dynamic numbers, booleans, and `nil` compare by value and kind.
* Strings compare with `strcmp`.
* Native operands compare with C operators.
* Values of different kinds are never equal, so `1 == true` is false in a
  dynamic context.

### Concatenation

`..` converts both operands to strings and joins them:

```ccpl
say("n=" .. 42 .. "!")
```

### Logic

`and` and `or` short circuit and return one of their operands. Each operand is
evaluated once, including when the left side decides the result. `not` always
returns a boolean.

```ccpl
var v = nil or "fallback"
```

### Casts, pointers, and indexing

```ccpl
var x: i32 = 42
var p: i32* = &x
*p = 99
var q: i64 = (i64) x
var b: i8* = malloc(8)
b[0] = 65
```

`&` takes an address, `*` dereferences, and `base[index]` indexes. Multi level
pointers work:

```ccpl
var p: u8** = malloc(8)
var q: u8* = malloc(8)
p[0] = q
q[0] = 65
say(p[0][0])
```

## Functions

```ccpl
func name(a, b)
    return a + b
end
```

Parameters are dynamic unless annotated. A colon introduces a native parameter
type or a native return type:

```ccpl
func add(a: i32, b: i32): i32
    return a + b
end
```

Functions may be defined anywhere at the top level and are visible to the whole
program, including their own bodies. Recursion works. There are no closures:
a nested function does not capture the enclosing function's variables.

Reusing a builtin name is an error.

## Globals and blocks

A top level `var` is a C global with a unique internal name. Top level
statements still run in order inside the generated entry point, so a global is
initialized when execution reaches its declaration. A function called before
that point sees the zero or `nil` initial value.

A block is the body of an `if` branch, a loop, or a function. Declarations in a
block are not visible after the block. This applies to `if`, `elseif`, `else`,
`while`, `repeat`, and `for`.

## Modules with `get`

```ccpl
get "examples/mathlib"
get "examples/mathlib/square"
```

A `get` statement may only appear at the top level of a file. A module path is
resolved relative to the importing file, then from the
compiler's `packages` directory, then from the sibling `packages` directory.

A whole module or one function may be imported. The same definition is included
at most once, even when several imports ask for parts of one module. A module's
top level statements run once, at the position of the first import.

Top level declarations from a module become globals, so functions from that
module can use module state. All modules share one global namespace, so avoid
reusing a top level name in different modules.

## Builtins

* `say(...)` prints values separated by tabs and then a newline.
* `hey(...)` prints values to standard error with a `hey:` prefix.
* `oh(msg)` prints `fatal: msg` to standard error and exits with status 1.
* `tonumber(x)` converts to a number, or returns `nil`.
* `tostring(x)` converts to a string.
* `malloc(n)` allocates `n` bytes.
* `calloc(n, size)` allocates and zeroes `n * size` bytes.
* `realloc(p, n)` resizes an allocation.
* `free(p)` frees an allocation.
* `pyexec(code)` runs Python statements and returns `nil`.
* `pyeval(code)` evaluates a Python expression and converts the result.

## Foreign functions

Foreign declarations use this form:

```ccpl
extern "c" func strlen(s: cstr): u64 from "msvcrt.dll"
extern "c++" func vec_len(x: f64, y: f64): f64 from "vec.dll" as "?vec_len@@YANNN@Z"
extern "python" func sqrt(x: f64): f64 from "math"
```

C and C++ declarations require native parameter types. A C declaration without
a return type is treated as returning void. Python declarations accept dynamic
arguments and convert the result according to the return annotation.

See [ffi.md](ffi.md) for the complete guide.

## Full grammar

```ebnf
program    := { stmt }

stmt       := var_decl | func_def | extern_def | if_stmt | while_stmt
            | repeat_stmt | for_stmt | return_stmt | "break" | "continue"
            | get_stmt | assign | call_stmt

var_decl   := ("var" | "variable") [ type ] NAME [ ":" type ] [ "=" expr ]
func_def   := "func" NAME [ "(" [ param { "," param } ] ")" ] [ ":" type ]
              { stmt } "end"
extern_def := "extern" STRING "func" NAME
              [ "(" [ param { "," param } ] ")" ] [ ":" type ]
              "from" STRING [ "as" STRING ]
param      := NAME [ ":" type ]
if_stmt    := "if" expr "then" { stmt }
              { "elseif" expr "then" { stmt } }
              [ "else" { stmt } ] "end"
while_stmt := "while" expr "do" { stmt } "end"
repeat_stmt:= "repeat" { stmt } "until" expr
for_stmt   := "for" NAME "=" expr "," expr [ "," expr ] "do" { stmt } "end"
return_stmt:= "return" [ expr ]
get_stmt   := "get" STRING
assign     := lvalue "=" expr
call_stmt  := NAME "(" [ expr { "," expr } ] ")"
lvalue     := NAME [ "[" expr "]" ] | "*" unary

type       := TYPEKW { "*" }

expr       := binop
binop      := unary { op expr }
unary      := "-" unary | "not" unary | "*" unary | "&" unary | postfix
postfix    := primary { "(" [ expr { "," expr } ] ")" | "[" expr "]" }
primary    := NUMBER | STRING | NAME | "nil" | "true" | "false"
            | "(" expr ")" | "(" type ")" unary
```