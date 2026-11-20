# Sun Language Specification

**Version:** Unknown (Stable)
**Status:** describes the language implemented by this repository; not yet a
stability or compatibility promise.

This document defines Sun's currently supported source-language behavior.
Examples in [HOWUSE.md](HOWUSE.md) are tutorials. The compiler's tests are the
executable conformance suite; if observed behavior conflicts with this draft,
the discrepancy should be resolved by fixing the implementation or explicitly
amending the specification. Intentional behavior changes should update this
document and focused conformance tests together.

## 1. Programs and compilation

A Sun source file is UTF-8 text with the `.su` extension. By default, the
compiler checks and translates it to C++17; an experimental LLVM backend is
also available through `--emit-llvm` and `--llvm`. `--check` performs source
validation without writing generated code or invoking the C++ compiler. A
compiled program runs as a native process with the host process's privileges.

Top-level declarations include functions, custom types, imports, and global
`inline cpp` blocks. Executable statements belong in functions or the main
program body. Imports must precede declarations and executable statements.
Local modules may import other local modules, but import cycles are errors and
top-level executable statements in an imported module are not supported.

The generated C++ is an implementation detail, not part of Sun's source
compatibility contract. C++ inline blocks are an explicit interoperability
escape hatch: they are compiled as native C++, are not validated or sandboxed
by Sun, and may access any resource available to the process.

## 2. Lexical rules

- Identifiers are case-sensitive. Keywords cannot be used as identifiers.
- Double-quoted strings support `\\`, `\"`, `\n`, `\t`, `\r`, and `\0`.
  Literal NUL bytes in source are rejected. String operations preserve UTF-8
  bytes unless a particular operation is documented as code-point-aware.
- `f"..."` starts an interpolated string. `{expression}` inserts an expression;
  `{{` and `}}` stand for literal braces. Empty and unmatched interpolations
  are compile-time errors.
- `#` begins a comment outside strings. Within `inline cpp`, C++ comment rules
  apply and the block ends at `endcpp`.
- Indentation is stylistic; `end` closes Sun blocks.

## 3. Types and values

The scalar types are `string`, `number`, `bool`, `int`, and `char`. `number` is
an IEEE-754 C++ `double`; `int` is a signed 64-bit integer; `char` is one valid
Unicode code point. `str` is accepted as an alias where a string type is
requested. Parameters without an annotation default to `string`.

The supported compound types include homogeneous `list[T]`, `dict[K, V]`,
fixed-size immutable `tuple[T1, ...]`, and user-declared structs, classes, and
enums. `region` and opaque `ptr` require `import memory`; `bytes` is a mutable
sequence of octets; dynamic JSON values require `import json`.

Variables and parameters are statically typed. Assignment requires the same
type, except that `int` may be promoted to `number`, and an empty list or
dictionary literal may initialize a compatible typed collection. There are no
implicit conversions from `number` to `int`, between unrelated custom types,
or from a derived class to its base class.

`const name: T = expression` declares an immutable compile-time value. The
initializer must be a supported, safely evaluable constant expression of the
declared type; expressions that can fail at runtime are not folded into
constants.

## 4. Expressions

Parentheses group expressions. Postfix calls, member access, and indexing bind
most tightly, followed by unary `-` and `not`. Binary operators are
left-associative, in this precedence order from low to high:

| Operators | Meaning |
| --- | --- |
| `or` | Boolean disjunction; short-circuits |
| `and` | Boolean conjunction; short-circuits |
| `==`, `!=`, `<`, `<=`, `>`, `>=` | Equality and ordered comparison |
| `+`, `-` | Addition/subtraction; `+` also concatenates strings |
| `*`, `/`, `%` | Multiplication, division, remainder |

Logical operators and conditions require `bool`. Ordered comparisons require
numeric operands. Equality requires matching types, except `int` and `number`
may be compared together. Arithmetic on two `int` operands uses checked
64-bit operations; overflow and division by zero raise a catchable runtime
error. Integer division truncates toward zero. Mixed `int`/`number` arithmetic
promotes the integer to `number`.

F-string interpolations are ordinary Sun expressions converted to text.
Pointers and custom error values cannot be interpolated directly. `%v`
formatted printing accepts named `%v=value` substitutions; `%%` represents a
literal percent sign.

## 5. Declarations and control flow

Functions use `def name(parameters): return_type ... end`. A missing return
annotation means `void`; every path in a non-void function must return a value
of the declared type. Parameters may have trailing constant defaults. Named
arguments are supported for Sun functions, methods, and constructors. Typed
`*args` and `**kwargs` collect extra positional and keyword arguments.

`let name: T = expression` declares a mutable local; `const` declares an
immutable one. `return`, `break`, and `continue` are valid only in their
respective function or loop contexts. Nested functions capture visible outer
variables by reference.

`if`/`elif`/`else` and `while` conditions must be boolean. `for name in
range(start, stop[, step])` iterates numbers from the inclusive start toward,
but excluding, the stop. A zero step or a step that cannot advance raises a
runtime error. `for name in collection` iterates supported lists, dictionaries,
and byte sequences. `match` selects one matching scalar or enum variant; enum
and boolean matches must be exhaustive unless an `else` arm is present.

## 6. Errors

Compile-time lexical, syntax, name-resolution, and type errors prevent code
generation. Runtime Sun errors are represented by `AnError` and may be raised
or caught with `try` and ordered `except Type [as name]` handlers. `as name`,
when present, receives the error message as a string. `AnError` is the catch-all
Sun error type and should follow more specific handlers. `finally` executes on
normal exit, return, and handled or unhandled error paths.

`inline cpp` exceptions are not automatically Sun errors. `cppCatcher` is the
explicit mechanism for catching exceptions thrown by inline C++ code. Native
failures not translated to Sun errors are not guaranteed to be catchable by
Sun handlers.

## 7. Modules and standard library

`import math`, `memory`, `io`, `json`, and `sys` enable their documented
standard-library APIs. Local modules are imported using a relative module path
and alias, for example `import local/geometry as geometry`. `export def`
defines an explicit public function API; for compatibility, a module with no
explicit exports exposes its top-level functions.

The exact APIs, argument rules, and runtime checks for these modules are
documented in the corresponding sections of [HOWUSE.md](HOWUSE.md). Unsupported
features are not implied by this specification: in particular, Sun currently
has no generics, inheritance is single-base, tuple indexing must be a
non-negative integer literal, and `inline cpp` is not safe for untrusted code.

## 8. Experimental LLVM backend

The optional LLVM backend supports scalar values, strings, tuples, nested
lists and dictionaries, structs and classes with fields, constructors,
methods and simple inheritance, enums and enum `match`, local functions with
by-reference captures, `with open`, control flow, and typed Sun exceptions
including `finally`. It remains experimental and does not implement full
parity with the default C++ backend. In particular, `inline cpp`,
`cppCatcher`, and the complete `json`, `memory`, and `sys` module APIs are not
available there. The exact supported cases are covered by the LLVM-specific
tests and documented in [HOWUSE.md](HOWUSE.md).
Equality, `contains`, and `remove` on pointer-backed values may use pointer
identity rather than recursive value equality.

## 9. Conformance and evolution

The compiler's test suite is the executable conformance suite for this draft:
`make test`. A behavior change is complete only when its specification and
focused regression tests are updated. The specification version remains
`0.1` while syntax, type rules, and runtime behavior are still evolving.
