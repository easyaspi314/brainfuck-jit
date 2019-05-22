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

/// VirtualAlloc wrappers for JIT on Windows
#ifndef BRAINFUCK_JIT_WINDOWS_H
#define BRAINFUCK_JIT_WINDOWS_H
#ifndef _WIN32
#   error "This code is for Windows"
#endif

#ifndef BRAINFUCK_JIT_C
#   error "This file is only to be included from brainfuck-jit.c"
#endif

#include <stddef.h>
#include <Windows.h>
#include <memoryapi.h>

// Windows can't just use the VirtualAlloc block, if it isn't volatile it gets optimized into oblivion.
static unsigned char *alloc_opcodes(size_t len)
{
    return (unsigned char *)malloc(1, len);
}

// Maps a section of virtual memory using VirtualAlloc, copies the opcodes into it, and runs it.
// We need buf to be volatile because optimizations mess with it.
static void run_opcodes(const unsigned char *restrict opcodes, size_t len, unsigned char *restrict cells)
{
    volatile unsigned char *buf =
        (volatile unsigned char *)VirtualAlloc(NULL, len, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (buf) {
        // Copy over the opcodes
        memcpy((void *)buf, opcodes, len);
        // Protect memory with R^X mode
        DWORD old;
        VirtualProtect((void *)buf, len, PAGE_EXECUTE_READ, &old);
        brainfuck_t fuck = (brainfuck_t)buf;
        fuck(cells, &putchar, &getchar);
        VirtualFree((void *)buf, len, MEM_RELEASE);
    } else {
        puts("Out of memory!");
    }
}

// Frees the opcodes buffer
static void dealloc_opcodes(unsigned char *buf, size_t len)
{
    (void)len;
    free(buf);
}

#endif // BRAINFUCK_WINDOWS_JIT_H
