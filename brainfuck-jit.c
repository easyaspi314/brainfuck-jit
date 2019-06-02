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

#define BRAINFUCK_JIT_C 1

#include <stdio.h> // printf
#include <stdlib.h> // calloc
#include <string.h> // memset, memcpy
#include <stdint.h> // uintN_t, intN_t
#ifndef __cplusplus
#   include <stdbool.h> // bool
#endif

#include "brainfuck-jit.h"

#if !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#    if defined(__GNUC__) || defined(_MSC_VER)
#        define restrict __restrict
#    else
#        define restrict
#    endif
#endif

#ifdef DEBUG
#   define bf_log(...) printf(__VA_ARGS__)
#else
#   define bf_log(...) ((void)0)
#endif

typedef void (*brainfuck_t)(uint8_t *cells_ptr, int (*putchar_ptr)(int), int (*getchar_ptr)(void));

// Instruction modes. Subtracting is treated as negative addition.
// All should fit in unsigned char!
typedef enum {
    bf_opcode_add = '+',
    bf_opcode_sub = '-', // unused
    bf_opcode_move = '>',
    bf_opcode_move_left = '<', // unused
    bf_opcode_put = '.',
    bf_opcode_get = ',',
    bf_opcode_start = '[',
    bf_opcode_end = ']',
    bf_opcode_clear = '0',
    bf_opcode_copy_mul = '*',
    bf_opcode_ret = 'r',
    bf_opcode_nop = '\0',// 'n' | ((int)'n' << 8) | ((int)'n' << 16) | ((int)'n' << 24)
} bf_opcode_type;

typedef struct {
    int op;
    int32_t amount;
} bf_opcode;
#ifdef C_BACKEND
#include "brainfuck-backend-c.h"
#else
#if !defined(USE_FALLBACK) && (defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64) || defined(__i386__) || defined(_M_IX86))
#   define JIT_MODE 1 // x86
#elif !defined(USE_FALLBACK) && (defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64))
#   define JIT_MODE 2 // aarch64
#elif !defined(USE_FALLBACK) && defined(__arm__) && __ARM_ARCH >= 5
#   define JIT_MODE 3 // ARMv5
#else
#   define JIT_MODE 0 // Fallback interpreter.
#endif

#if JIT_MODE == 0
#   include "brainfuck-interp.h"
#else

// Should include the following implementations:
//    // The longest instruction for allocating a large enough buffer
//    #define MAX_INSN_LEN N
//    // The size of the init routine
//    #define INIT_LEN N
//    // The size of the cleanup routine
//    #define CLEANUP_LEN N
//    // Converts a bf_opcode into native code, incrementing pos
//    static void compile_opcode(bf_opcode *restrict opcode, uint8_t *restrict out, size_t *restrict pos)

#   if JIT_MODE == 1
#      include "brainfuck-jit-x86.h"
#   elif JIT_MODE == 2
#      include "brainfuck-jit-aarch64.h"
#   elif JIT_MODE == 3
#      include "brainfuck-jit-arm.h"
#   else
#      error "Unknown CPU target"
#   endif

// should include the following implementations:
//     // Allocates the opcodes.
//     static unsigned char *alloc_opcodes(size_t amount);
//     // Executes the code.
//     static void run_opcodes(unsigned char *buf, size_t len, unsigned char *cells);
//     // Deallocates the opcodes
//     static void dealloc_opcodes(unsigned char *buf, size_t len);

#  if defined(_WIN32) || defined(__CYGWIN__)
#     include "brainfuck-jit-mmap-windows.h"
#  elif defined(__unix__)
#     include "brainfuck-jit-mmap-unix.h"
#  else
#     error "Unknown OS!"
#  endif
#  include "brainfuck-jit-runner.h"
#endif
#endif
#include "brainfuck-ir.h"


// Executes the code with light JIT optimization.
void brainfuck(const char *code, size_t len)
{
    int32_t mode = bf_opcode_nop, combine = 0;
    bool leaf = false;

    {
        volatile int32_t neg1 = -1;
        if (neg1 >> 5 != -1) {
            printf("Need 2's complement arithmetic shift right!\n");
            exit(1);
        }
    }

    // Our stack to hold loop pointers. We use the worst case scenario in which every char is a loop starter so we don't need to realloc.
    // This prevents a lot of checking at the cost of more memory.
    bf_opcode **loops = (bf_opcode **)malloc(len * sizeof(bf_opcode *));
    if (!loops) {
        printf("out of memory\n");
        exit(1);
    }
    bf_opcode **loops_iterator = loops;

    bf_opcode *opcodes = (bf_opcode *)calloc(len + 128, sizeof(bf_opcode));

    if (!opcodes) {
        printf("out of memory\n");
        free(loops);
        exit(1);
    }

    bf_opcode *opcodes_iterator = opcodes;

    for (size_t i = 0; i < len; i++) {
        // We don't actually output opcodes immediately after reading an instruction. We actually
        // delay by at least one instructions so we can join consecutive +- and <>s.
        //
        // As soon as we reach a loop, dot, or commma, or we end a chain of +- or <>, we call commit() and
        // it will write to the opcodes_iterator. combine holds the number of additions or subtractions
        // we use.
        switch (code[i]) {
        case bf_opcode_add: // increment value at pointer
            if (mode != bf_opcode_add) {
                commit(mode, combine, &opcodes_iterator);
                mode = bf_opcode_add;
                combine = 0;
            }
            ++combine;
            break;
        case bf_opcode_sub: // decrement value at pointer
            if (mode != bf_opcode_add) {
                commit(mode, combine, &opcodes_iterator);
                mode = bf_opcode_add;
                combine = 0;
            }
            --combine;
            break;
        case bf_opcode_move: // increment pointer
            if (mode != bf_opcode_move) {
                commit(mode, combine, &opcodes_iterator);
                mode = bf_opcode_move;
                combine = 0;
            }
            ++combine;
            break;
        case bf_opcode_move_left: // decrement pointer
            if (mode != bf_opcode_move) {
                commit(mode, combine, &opcodes_iterator);
                mode = bf_opcode_move;
                combine = 0;
            }
            --combine;
            break;
        case bf_opcode_start: // begin loop
            commit(mode, combine, &opcodes_iterator);
            mode = bf_opcode_start;
            combine = 0;
            leaf = true;
            *loops_iterator++ = opcodes_iterator; // push the address of the next opcode to the stack
            break;
        case bf_opcode_end: { // end loop
            if (loops_iterator == loops) { // if our stack is empty, fail
                printf("position %zu: Extra ']'n", i);
                free(loops);
                free(opcodes);
                exit(1);
            }
            // Pop from our stack
            bf_opcode *start = *--loops_iterator;

            // Basic optimization: Convert clear loops ([-] and [+]) to *cell = 0;
            // Because of our delayed commit system, we can tell if this happened if we
            // haven't moved from the previous bf_opcode_begin instruction (opcodes_iterator hasn't been
            // incremented since then), and the mode of the previous instruction was bf_opcode_add.
            //
            // We only do it with odd increments: Since arithmetic is modulo 256, something like
            // [--] isn't guaranteed to not be an infinite loop.
            if (start == opcodes_iterator - 1 && mode == bf_opcode_add) {
                 bf_log("converting clear loop!!!\n");
                 write_clear_loop(start, &opcodes_iterator);
                 // Reset the mode
                 mode = bf_opcode_nop;
                 combine = 0;
                 // break early
                 break;
            }

            commit(mode, combine, &opcodes_iterator);
            mode = bf_opcode_end;
            combine = 0;
            if (fill_in_jump(start, &opcodes_iterator, leaf)) {
                mode = bf_opcode_nop;
            }
            leaf = false; // not in a leaf anymore
            break;
        }
        case bf_opcode_put: // putchar()
            commit(mode, combine, &opcodes_iterator); // always commit
            mode = bf_opcode_put;
            combine = 0;
            break;
        case bf_opcode_get: // getchar()
            commit(mode, combine, &opcodes_iterator);
            mode = bf_opcode_get;
            combine = 0;
            break;
        default: // ignore
            break;
        }
    }
    if (loops_iterator != loops) { // missing ]
        printf("Position %zu: Missing ]\n", len - 1);
        free(opcodes);
        free(loops);
        exit(1);
    }

    // We don't need this anymore.
    free(loops);

    // Last call to commit() to handle the final instruction
    commit(mode, combine, &opcodes_iterator);
    size_t opcodes_len = opcodes_iterator - opcodes;

    // Convert to machine code and run
    run_opcodes(opcodes, opcodes_len);
    free(opcodes);
}

