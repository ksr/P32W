# p32cc — the P32W C compiler

The same compiler in C, which runs on P32W Unix and compiles itself there,
is `software/cc` (M7, D24); the two write the same assembly (`make test`).

**Status:** M2 of the operating-system plan (D22, [os-design.md](../docs/os-design.md)).
Compiles the C library and the test programs; every test program behaves
exactly like the host compiler's build of it (`make test`).

```
python3 cc/p32cc.py -c file.c [-o file.o]       compile to an object
python3 cc/p32cc.py -S file.c                   assembly (file.s), with each statement's source line
python3 cc/p32cc.py -E file.c                   preprocessed source
python3 cc/p32cc.py --sa prog.c -o prog.bin     a stand-alone program for the emulators
emulator/p32emu prog.bin < /dev/null            run it (exit status = main's result)
```

`-I DIR`, `-D NAME[=VAL]`, `-w` (no warnings), `-M map` (link map).

## The language

C89, with floating point in software (P32W has no FPU):
`char` (signed) / `short` / `int` / `long` (32 bits), `unsigned`, pointers,
arrays, `struct`, `union`, `enum`, `typedef`, function pointers, `static` /
`extern`, initialisers (nested, partial, strings, address constants),
`switch`, `goto`, variadic functions (`stdarg.h`), the full preprocessor
(`#include`, `#define` with `#` / `##` / `__VA_ARGS__`, `#if` / `#elif`,
`defined`, `__FILE__` / `__LINE__`).

From C99: `//` comments, declarations anywhere in a block and in `for (int
i = ...)`, `_Bool`, `inline` (accepted, no effect), **designated
initialisers** (`[i] =`, `.member =`, chains like `.a.b[2] =`), brace
elision in initialisers, **compound literals** (`(struct pt){ 1, 2 }`, at
file scope a static object, in a function a fresh one each time it is
evaluated) and `__func__`.

**Bit-fields**, laid out as System V does (from a unit's lowest bit, never
across a unit the size of the field's type; `:0` ends the unit; plain `int`
fields are signed), with `op=` and `++`/`--` (evaluating the struct's place
once) and in initialisers.

**Structs by value**, as arguments and results (D30): a function with a
struct parameter takes every argument on the stack, each struct copied
into words of its own (as a variadic function does, so `va_arg(ap, struct
pt)` works); a function returning a struct gets a hidden first argument,
where to put it, and returns that address.

**`long long`** (64 bits, aligned to 4; D30): kept in memory like a
struct, so it passes and returns as one; its operations are calls of
libc's helpers (`libc/llong.c`: `__lladd` ... `__llcmp`), constants are
objects in the data, and constant expressions fold at compile time.
printf has `%lld`, stdlib `strtoll` and its family.

**`double`** (D30; `float` and `long double` are the same 8-byte IEEE
type, which C permits): kept in memory like a long long, its operations
calls of libc's helpers (`libc/dbl.c`, on softfp, so + - * / are
correctly rounded and agree bit for bit with any IEEE machine).
Comparisons follow C's NaN rules. Constants (`1.5`, `.5e-3`, `2.f`) are
objects in the data and fold at compile time, except a division by zero
or a NaN result.

**Not yet:** K&R-style definitions, hexadecimal floating constants. A use is a clear
compile-time error. (In progress, D30: the C compiler upgrade.)

## How it works

| File | Stage |
|---|---|
| `clex.py` | tokens with file, line and spacing |
| `cpp.py` | the preprocessor, on tokens (hidesets stop recursive macros) |
| `cparse.py` | recursive-descent parser: a typed syntax tree, C89 conversions, constants folded |
| `cty.py` | types: sizes, alignment, struct layout, the usual arithmetic conversions |
| `cgen.py` | P32W assembly for `p32asm -c` |
| `p32cc.py` | the driver: assemble with `p32asm`, link with `p32ld` |

**Calling convention** (D22, D30): arguments 1–4 in r1–r4, more on the
stack (argument 5 lowest); result in r1; r1–r7 caller-saved, r8–r13
callee-saved, r14 frame pointer, r15 stack pointer; `CALL` pushes the
return address. A **variadic** function, or one with a struct parameter,
gets every argument on the stack (a struct in as many words as it
takes), so `va_list` is a plain pointer. A struct result goes where a
hidden first argument points. Arguments are evaluated left to right.

**Code:** expressions are evaluated in a stack of temporaries (the lowest
free of r1–r13); locals live in the frame (`off(r14)`); immediates are used
where they fit; conditions compile to direct compare-and-branch. No
optimiser yet.

## Tests

`cc/tests/*.c`, run by `cc/run_tests.py` (in `make test`): each program is
built by p32cc and run in `p32emu`, and built by the host `cc` and run
natively; output and exit status must match. A `NAME.expect` file makes a
test P32W-only.
