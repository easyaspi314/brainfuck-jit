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


/// brainfuck-jit-fallback.h: Portable, non-JIT interpreter. Still has decent performance.
#ifndef BRAINFUCK_JIT_FALLBACK_H
#define BRAINFUCK_JIT_FALLBACK_H

#ifndef BRAINFUCK_JIT_C
#  error "This file is only to be included from brainfuck-jit.c"
#endif
#include <string.h>

// All instructions are 8 bytes. This is wasteful, but fast to parse and calculate.
#define MAX_INSN_LEN sizeof(int) * 2
// size of init[] - no op
#define INIT_LEN 0
// size of cleanup[]
#define CLEANUP_LEN sizeof(int) * 2
// jmp = 5 bytes
#define JUMP_INSN_LEN sizeof(int) * 2

#define INSN_ADD_PTR ADD_POINTER
#define INSN_ADD_DATA ADD_DATA
#define INSN_JUMP JUMP
#define INSN_CMP CMP
#define INSN_PUT_CHAR PUT_CHAR
#define INSN_GET_CHAR GET_CHAR
#define INSN_ZERO '0'

// Interprets our pre-parsed format.
static void interpret_opcodes(int *restrict opcodes, size_t len, unsigned char *restrict cell)
{
    size_t i= 0;
    int current = 0;
    while (i < len) {
        int mode = opcodes[i++];
        int value = opcodes[i++];

        switch (mode) {
        case INSN_ADD_PTR:
#ifdef DEBUG
            printf("cell += %d;\n", value);
#endif
            *cell = current;
            cell += value;
            current = *cell;
            break;
        case INSN_ADD_DATA:
#ifdef DEBUG
            printf("*cell += %d;\n", value);
#endif
            current += value;
            break;
        case INSN_PUT_CHAR:
#ifdef DEBUG
            printf("putchar(%d /* %c */ );\n", *cell, *cell);
#endif
            putchar(current);
            break;
        case INSN_GET_CHAR:
            current = getchar();
#ifdef DEBUG
            printf("*cell = getchar(); /* %i */;\n", *cell);
#endif

            break;
        case INSN_ZERO:
#ifdef DEBUG
            printf("*cell = 0;\n");
#endif
            current = 0;
            break;
        case INSN_JUMP:
#ifdef DEBUG
            printf("if (%i == 0) {\n i += %i;\n}\n", *cell, value);
#endif
            if (current == 0) {
                i += value;
            }
            break;
        case INSN_CMP:
#ifdef DEBUG
            printf("if (%i != 0) {\n    i += %i;\n}\n", *cell, value);
#endif
            if (current != 0) {
                i += value;
            }
            break;
       default:
            break;
       }
   }
}

// Allocates our block
static unsigned char *alloc_opcodes(size_t len)
{
    return (unsigned char *)calloc(1, len);
}

// Runs our interpreter
static void run_opcodes(unsigned char *restrict opcodes_raw, size_t len, unsigned char *restrict cell)
{
    int *opcodes = (int *)opcodes_raw;
    len /= sizeof(int); // we use sizeof(int) for our iterator
    interpret_opcodes(opcodes, len, cell);
}

// Free the block of opcodes
static void dealloc_opcodes(unsigned char *opcodes, size_t len)
{
    (void)len;
    free(opcodes);
}

// Writes the initialization code for our interpreter.
// We don't do anything.
static void write_init_code(unsigned char **out)
{
    (void)out;
}

// Don't bother switching. We just store and move on. This prevents a lot of calculations.
static void commit(int mode, int amount, unsigned char **out)
{
    if (mode == 0)
        return;
#ifdef DEBUG
   printf("commit: %c: %d\n", mode, amount);
#endif
    int *opcodes_ptr = (int *)(*out);
    *out += 2 * sizeof(int);
    opcodes_ptr[0] = mode;
    if (mode == ADD_POINTER || mode == ADD_DATA)
        opcodes_ptr[1] = amount;
}

// Fills in the offset for a jump.
static void fill_in_jump(unsigned char *restrict start_raw, unsigned char *restrict opcodes_iterator_raw)
{
    int *start = (int *)start_raw;
    int *opcodes_iterator= (int *)opcodes_iterator_raw;
    int diff = start - opcodes_iterator - 2;
    opcodes_iterator[1] = diff;
    diff = opcodes_iterator - start;
    start[-1] = diff;
}

// Writes the cleanup code for our interpreter. A.K.A. nothing.
static void write_cleanup_code(unsigned char **out)
{
    (void)out;
}

// Overwrites a clear loop with a zero loop
static void write_clear_loop(unsigned char *restrict start, unsigned char **restrict out)
{
    int *opcodes_iterator = (int *)start;
    opcodes_iterator[0] = INSN_ZERO;
    *out = start + 2 * sizeof(int);
}

#endif // BRAINFUCK_JIT_FALLBACK_H

