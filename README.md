# brainfuck-jit

A lightly optimizing JIT Brainfuck interpreter for x86_64 Unix.

i386 (as well as ARM, PPC, etc) and Windows are intentionally not supported.

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

 - All cells are 8-bit unsigned integers
 - 32768 cells, allocated in stack memory
 - Pointer starts at the beginning of the tape
 - Writing out of bounds is UB
 - Mismatched `[]`s are treated as errors during compilation.
 - `EOF` from `,` is treated as `0xFF`. 

The compiler is single pass, and its optimizations are limited to joining consecutive
`+-` and `<>` and replacing clear loops (`[-]` or `[+]`). That is usually enough to
provide decent performance.

Internally, the compiler uses `mmap` to allocate a block of executable memory, and then
executes it. Unlike some JIT implementations which use `syscall`, this uses function
pointers to `getchar` and `putchar` (as well as `memset`). This means that this has
access to fully buffered IO, and the memory is cleared using a quality implementation
instead of a naive loop.

This is written in POSIX C99, but the code is compatible with a C++ compiler if you
prefer.
