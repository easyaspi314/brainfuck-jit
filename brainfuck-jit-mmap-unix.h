/*
 * Copyright (c) 2019 easyaspi314
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#ifndef BRAINFUCK_JIT_UNIX_H
#define BRAINFUCK_JIT_UNIX_H
#ifndef __unix__
#   error "This code is for Unix."
#endif

#ifndef BRAINFUCK_JIT_C
#   error "This file is only to be included from brainfuck-jit.c"
#endif
#include <string.h>
#include <stddef.h>
#include <sys/mman.h>

static unsigned char *alloc_opcodes(size_t len)
{
#ifdef UNSAFE // -DUNSAFE: Writes in RWX mode. Slightly faster, but less safe
    int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
#else
    int prot = PROT_READ | PROT_WRITE;
#endif
    int flags = MAP_ANONYMOUS | MAP_PRIVATE;
    unsigned char *buf = (unsigned char *)mmap(NULL, len, prot, flags, -1, 0);
    return buf;
}

// Allocates a buffer using mmap, copies opcodes, marks executable, and runs.
static void run_opcodes(unsigned char *restrict buf, size_t len, unsigned char *restrict cells)
{
#ifndef UNSAFE
    // Mark our region as R^X
    mprotect(buf, len, PROT_READ | PROT_EXEC);
#endif
    // Cast to a function pointer
    brainfuck_t fuck = (brainfuck_t)buf;
    // and fuck it!
    fuck(cells, &putchar, &getchar);
}

// Unmaps our buffer
static void dealloc_opcodes(unsigned char *buf, size_t len)
{
    munmap(buf, len);
}

#endif // BRAINFUCK_JIT_UNIX_H

