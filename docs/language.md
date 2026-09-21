# The CCPL language reference

CCPL is a small dynamically-typed language with optional native C types. This
document describes the syntax and semantics of the language as implemented by the
compiler in `src/compiler.c`.

- [Lexical structure](#lexical-structure)
- [Types and values](#types-and-values)
- [Statements](#statements)
- [Expressions](#expressions)
- [Functions](#functions)
- [Modules with `get`](#modules-with-get)
- [Builtins](#builtins)
- [Full grammar](#full-grammar)

## Lexical structure

### Whitespace and comments

Whitespace, tabs and carriage returns are ignored. Newlines separate statements,
but a semicolon (`;`) also separates statements on the same line.

```ccpl
-- a line comment runs to the end of the line
--[[
    a block comment
    can span many lines
]]
```

### Identifiers and keywords

Identifiers start with a letter or `_` and continue with letters, digits or `_`.
The following are reserved keywords:

```
var variable func end if then elseif else while do repeat until for
return break continue get and or not true false nil
i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char ptr
```

### Numbers

- Decimal integers: `42`, `-7`
- Floating point: `3.14`, `.5`, `2.5e-2`, `1e3`
- Hexadecimal: `0xFF`, `0x10`

### Strings

Strings are double-quoted and support these escapes:

| Escape | Meaning |
| --- | --- |
| `\n` `\t` `\r` `\a` `\b` `\f` `\v` | Control characters |
| `\\` | Backslash |
| `\"` | Double quote |
| `\ddd` | Decimal byte value, 1–3 digits (`\65` is `A`) |

Strings cannot contain a NUL byte; `\0` is rejected.

### Operators

```
( ) [ ] { } , ; = == ~= < <= > >= + - * / % .. & : .
```

## Types and values

A value is either **dynamic** or **native**.

### Dynamic values

Dynamic values are boxed and behave like Lua values:

| Kind | Examples | Notes |
| --- | --- | --- |
| Number | `42`, `3.14` | Always a double. |
| String | `"hi"` | Immutable. |
| Boolean | `true`, `false` | |
| Nil | `nil` | The absence of a value. |

`nil` and `false` are falsy. Everything else is truthy — including the number `0`,
as in Lua.

### Native values

Native values are plain C values with a declared type and use C semantics
(including zero being falsy).

```ccpl
var n: i32 = 42
var ratio: f64 = 0.5
var ok: bool = true
var p: i8* = malloc(16)
```

Native type keywords and their C equivalents:

| CCPL | C |
| --- | --- |
| `i8` `i16` `i32` `i64` | `int8_t` `int16_t` `int32_t` `int64_t` |
| `u8` `u16` `u32` `u64` | `uint8_t` `uint16_t` `uint32_t` `uint64_t` |
| `f32` `f64` | `float` `double` |
| `bool` | `bool` |
| `char` | `char` |
| `ptr` | `void *` |

A trailing `*` makes a pointer, and `*` may be repeated (`u8**`). You may write the
type before or after the name:

```ccpl
var p: i32* = &x
var i32* q = &x        -- same thing
```

### Conversions

- Assigning or passing a dynamic value where a native value is expected converts it
  with `tonumber`-like rules (the program aborts if it cannot).
- A native value assigned to a dynamic variable is boxed (pointers become numbers).
- Pointers can only be stored in native pointer variables.
- Cast explicitly with `(type)expr`.

## Statements

### Declarations

```ccpl
var a
var b = 1
variable c = "text"
var d: f64 = 2.5
```

A declaration without `=` initialises to `nil` (dynamic) or `0` (native).

### Assignment

```ccpl
x = 10
arr[i] = 3
*p = 99
```

### Conditionals

```ccpl
if cond then
    ...
elseif other then
    ...
else
    ...
end
```

### Loops

```ccpl
while cond do
    ...
end

repeat
    ...
until cond

for i = first, last [, step] do
    ...
end
```

`for` is inclusive of `last`; the optional step may be negative to count down.

`break` leaves the innermost loop and `continue` skips to its next iteration.

### Return

```ccpl
return expr
return
```

A function with no `return` yields `nil` (or `0` for a native return type).

## Expressions

Operator precedence, lowest to highest:

1. `or`
2. `and`
3. `==` `~=` `<` `<=` `>` `>=`
4. `..` (right associative)
5. `+` `-`
6. `*` `/` `%`

### Arithmetic

- Dynamic operands are doubles: `/` always produces a fraction and `%` is floor-mod.
- Native integer operands follow C for `/` (truncation), and `%` is floor-mod as well.
- Mixing a dynamic and a native operand promotes the expression to dynamic.

### Comparison and equality

- Dynamic numbers, booleans and `nil` compare by value/kind; strings compare with `strcmp`.
- Native operands compare with C operators.
- Values of different kinds are never equal (`1 == true` is false in dynamic context).

### Concatenation

`..` converts both operands to strings and concatenates them:

```ccpl
say("n=" .. 42 .. "!")      --> n=42!
```

### `and`, `or`, `not`

`and` and `or` short-circuit and return one of their operands (like Lua). `not`
always returns a boolean.

```ccpl
var v = nil or "fallback"   --> "fallback"
```

### Casts, pointers and indexing

```ccpl
var x: i32 = 42
var p: i32* = &x            -- address-of
*p = 99                     -- dereference
var q: i64 = (i64)x         -- cast
var b: i8* = malloc(8)
b[0] = 65                   -- pointer indexing
```

## Functions

```ccpl
func name(a, b)
    return a + b
end
```

Parameters are dynamic unless annotated. A colon introduces a native parameter
type or the native return type:

```ccpl
func add(a: i32, b: i32): i32
    return a + b
end
```

Functions may be defined anywhere at the top level and are visible to the whole
program (and to their own bodies, enabling recursion). Reusing a builtin name is an
error.

## Modules with `get`

```ccpl
get "mathlib"          -- import all top-level definitions
get "mathlib/square"   -- import just the `square` function
```

A module path is resolved relative to the importing file and then from the
compiler's `packages\` directories. Each module is imported once per compilation.

## Builtins

| Builtin | Signature | Description |
| --- | --- | --- |
| `say` | `say(...)` | Print values separated by tabs and a trailing newline. |
| `hey` | `hey(...)` | Print values to stderr. |
| `oh` | `oh(msg)` | Print `msg` to stderr and exit with an error. |
| `tonumber` | `tonumber(x)` | Convert to a number, or `nil`. |
| `tostring` | `tostring(x)` | Convert to a string. |
| `malloc` | `malloc(n)` | Allocate `n` bytes. |
| `calloc` | `calloc(n, size)` | Allocate and zero `n * size` bytes. |
| `realloc` | `realloc(p, n)` | Resize an allocation. |
| `free` | `free(p)` | Free an allocation. |

## Full grammar

```ebnf
program    := { stmt }

stmt       := var_decl | func_def | if_stmt | while_stmt | repeat_stmt
            | for_stmt | return_stmt | "break" | "continue"
            | get_stmt | assign | call_stmt

var_decl   := ("var" | "variable") [ type ] NAME [ ":" type ] [ "=" expr ]
func_def   := "func" NAME [ "(" [ param { "," param } ] ")" ] [ ":" type ]
              { stmt } "end"
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
lvalue     := NAME [ "[" expr "]" ] | ("*" | "&") unary

type       := TYPEKW { "*" }

expr       := binop
binop      := unary { op expr }        -- precedence per the table above
unary      := "-" unary | "not" unary | "*" unary | "&" unary | postfix
postfix    := primary { "(" [ expr { "," expr } ] ")" | "[" expr "]" }
primary    := NUMBER | STRING | NAME | "nil" | "true" | "false"
            | "(" expr ")" | "(" type ")" unary
```
