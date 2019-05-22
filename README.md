# brainfuck-jit

A lightly optimizing JIT Brainfuck interpreter for ARMv5+ and x86_64, as well as generic interpreter.

x86_64 and ARM JIT are supported on both *nix and Windows. Other platforms use a generic interpreter.

i386 and aarch64 JITs are planned as well.

```c
void brainfuck(const char *code, size_t len);
```

Usage:

```c
const char *code = "+[-[<<[+[--->]-[<<<]]]>>>-]>-.---.>..>.<<<<-.<+.>>>>>.>.<<.<-.";
brainfuck(code, strlen(code));
```

```
$ make
$ ./brainfuck-jit file.bf
```

The program will default run an example which prints the alphabet backwards, but you
can provide a single filename and it will run that instead.

### Brainfuck behavior

 - All cells are 8-bit unsigned integers.
 - 65535 cells, allocated in heap memory.
 - Pointer starts at the beginning of the tape.
 - Writing out of bounds is UB.
 - Mismatched `[]`s are treated as errors during compilation.
 - `EOF` from `,` is treated as `0xFF`. 

The compiler is single pass, and its optimizations are limited to joining consecutive
`+-` and `<>` and replacing clear loops (`[-]` or `[+]`). That is usually enough to
provide decent performance.

Internally, the compiler uses `mmap` (or `VirtualAlloc`) to allocate a block of
executable memory, and then executes it.

Unlike some JIT implementations which use `syscall`, this uses function pointers to
`getchar` and `putchar`. This means that this has access to fully buffered IO instead
of laggy syscalls.

This is written in C99, but the code is compatible with a C++ compiler if you
prefer.
