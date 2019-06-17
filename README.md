# brainfuck-jit

An optimizing JIT Brainfuck interpreter for x86_64, i386, ARM, x86, as well as generic interpreter and C converter.

i386, x86_64, ARM, and aarch64 JIT are supported on both *nix and Windows. Other platforms use a generic interpreter.

A minimal but fast standalone assembly wrapper for the debug output of x86_64 *nix is provided.


```c
void brainfuck(const char *code, size_t len, int optlevel);
```

Usage:

```c
const char *code = "+[-[<<[+[--->]-[<<<]]]>>>-]>-.---.>..>.<<<<-.<+.>>>>>.>.<<.<-.";
brainfuck(code, strlen(code), 2);
```

```
$ make
$ ./brainfuck-jit file.bf
```

The program will default run an example which prints the alphabet backwards, but you
can provide a single filename and it will run that instead. Add -O[n] as the first argument
to play with optlevel.

### Brainfuck behavior

 - All cells are 8-bit unsigned integers.
 - 65536 cells, allocated in heap memory.
 - Pointer starts at the beginning of the tape.
 - Writing out of bounds is UB.
 - Mismatched `[]`s are treated as errors during compilation.
 - `EOF` from `,` is treated as `0xFF`. 
 - optlevel turns on or off optimizations.
 - When `make DEBUG=1` is used, it outputs opcodes to `bf.s`, raw machine code to
   `bf.o`, and emits a bunch of debugging info.

The compiler will optimize consecutive operations with optlevel >= 1, and optimize
clear loops and multiply loops when optlevel >= 2.

Internally, the compiler uses `mmap` (or `VirtualAlloc`) to allocate a block of
executable memory, and then executes it.

Unlike some JIT implementations which use `syscall`, this uses function pointers to
`getchar` and `putchar`. This means that this has access to fully buffered IO instead
of laggy syscalls. The assembly wrapper also uses buffered IO in its syscall wrapper.

This is written in C99, but the code is compatible with a C++ compiler if you
prefer.
