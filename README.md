# brainfuck-jit

A lightly optimizing JIT Brainfuck interpreter for x86_64 Unix.

```c
void brainfuck(const char *code, size_t len);
```

Usage:

```c
const char *code = "+[-[<<[+[--->]-[<<<]]]>>>-]>-.---.>..>.<<<<-.<+.>>>>>.>.<<.<-.";
brainfuck(code, strlen(code));
```

