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

#include "brainfuck-jit.h"

#if !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#    if defined(__GNUC__) || defined(_MSC_VER)
#        define restrict __restrict
#    else
#        define restrict
#    endif
#endif

typedef void (*brainfuck_t)(unsigned char *cells_ptr, int (*putchar_ptr)(int), int (*getchar_ptr)(void));

// Instruction modes. Subtracting is treated as negative addition.
#define ADD_POINTER '>'
#define SUB_POINTER '<'
#define ADD_DATA '+'
#define SUB_DATA '-'
#define PUT_CHAR '.'
#define GET_CHAR ','
#define JUMP '['
#define CMP ']'

#if !defined(USE_FALLBACK) && (defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64))
#   define JIT_MODE 1 // x86_64
#elif !defined(USE_FALLBACK) && defined(__arm__) && __ARM_ARCH >= 5
#   define JIT_MODE 2 // ARM
#else
#   define JIT_MODE 0 // fallback
#endif

#if JIT_MODE == 0
#   include "brainfuck-jit-fallback.h"
#else
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

// Should include the following implementations:
//    // The longest instruction for allocating a large enough buffer
//    #define MAX_INSN_LEN N
//    // The size of the init routine
//    #define INIT_LEN N
//    // The size of the cleanup routine
//    #define CLEANUP_LEN N
//    // The number of bytes a jump instruction takes up to add to the loop stack
//    #define JUMP_INSN_LEN N
//    // Writes the initialization opcodes into *out and increments
//    static void write_init_code(unsigned char **out);
//    // Outputs opcodes into *out, given the mode and amount, incrementing *out by the number of bytes written
//    static void commit(int mode, int amount, unsigned char **out);
//    // Writes the cleanup opcodes into *out and increments
//    static void write_cleanup_code(unsigned char **out);
//    // The code to write the offset for a jump instruction into out. It is expected that commit will jump
//    // over the code inserted by this function.
//    static void fill_in_jump(unsigned char *start, unsigned char *opcodes_iterator);
//    // The code to overwrite a jump with a clear loop
//    static void write_clear_loop(unsigned char *start, unsigned char **out);
#   if JIT_MODE == 1
#      include "brainfuck-jit-x86_64.h"
#   elif JIT_MODE == 2
#      include "brainfuck-jit-arm.h"
#   else
#      error "Unknown CPU target"
#   endif
#endif

// Executes the code with light JIT optimization.
void brainfuck(const char *code, size_t len)
{
    int mode = 0, combine = 0;

    // Our stack to hold loop pointers. We use the worst case scenario in which every char is a loop starter so we don't need to realloc.
    // This prevents a lot of checking at the cost of more memory.
    unsigned char **loops = (unsigned char **)calloc(len, sizeof(unsigned char *));
    if (!loops) {
        printf("out of memory\n");
        exit(1);
    }
    unsigned char **loops_iterator = loops;

    // More than enough memory.
    size_t memlen = len * MAX_INSN_LEN + INIT_LEN + CLEANUP_LEN;
    // Map the memory for our opcodes.
    unsigned char *opcodes = alloc_opcodes(memlen);

    if (!opcodes) {
        printf("out of memory\n");
        free(loops);
        exit(1);
    }

    unsigned char *opcodes_iterator = opcodes;

    // Write the initialization opcodes.
    write_init_code(&opcodes_iterator);

    for (size_t i = 0; i < len; i++) {
        // We don't actually output opcodes immediately after reading an instruction. We actually
        // delay by at least one instructions so we can join consecutive +- and <>s.
        //
        // As soon as we reach a loop, dot, or commma, or we end a chain of +- or <>, we call commit() and
        // it will write to the opcodes_iterator. combine holds the number of additions or subtractions
        // we use.
        switch (code[i]) {
        case ADD_DATA: // increment value at pointer
            if (mode != ADD_DATA) {
                commit(mode, combine, &opcodes_iterator);
                mode = ADD_DATA;
                combine = 0;
            }
            ++combine;
            break;
        case SUB_DATA: // decrement value at pointer
            if (mode != ADD_DATA) {
                commit(mode, combine, &opcodes_iterator);
                mode = ADD_DATA;
                combine = 0;
            }
            --combine;
            break;
        case ADD_POINTER: // increment pointer
            if (mode != ADD_POINTER) {
                commit(mode, combine, &opcodes_iterator);
                mode = ADD_POINTER;
                combine = 0;
            }
            ++combine;
            break;
        case SUB_POINTER: // decrement pointer
            if (mode != ADD_POINTER) {
                commit(mode, combine, &opcodes_iterator);
                mode = ADD_POINTER;
                combine = 0;
            }
            --combine;
            break;
        case JUMP: // begin loop
            commit(mode, combine, &opcodes_iterator);
            mode = JUMP;
            combine = 0;
            *loops_iterator++ = opcodes_iterator + JUMP_INSN_LEN; // push the address of the next opcode to the stack
            break;
        case CMP: { // end loop
            if (loops_iterator == loops) { // if our stack is empty, fail
                printf("position %zu: Extra ']'n", i);
                free(loops);
                free(opcodes);
                exit(1);
            }
            // Pop from our stack
            unsigned char *start = *--loops_iterator;

            // Basic optimization: Convert clear loops ([-] and [+]) to *cell = 0;
            // Because of our delayed commit system, we can tell if this happened if we
            // haven't moved from the previous JUMP instruction (opcodes_iterator hasn't been
            // incremented since then), and the mode of the previous instruction was ADD_DATA.
            //
            // We only do it with odd increments: Since arithmetic is modulo 256, something like
            // [--] isn't guaranteed to not be an infinite loop.
            if (start == opcodes_iterator && mode == ADD_DATA && (combine & 1) == 1) {
#ifdef DEBUG
                 printf("converting clear loop!!!\n");
#endif
                 write_clear_loop(start, &opcodes_iterator);
                 // Reset the mode
                 mode = 0;
                 combine = 0;
                 // break early
                 break;
            }

            commit(mode, combine, &opcodes_iterator);
            mode = CMP;
            combine = 0;
            fill_in_jump(start, opcodes_iterator);
            break;
        }
        case PUT_CHAR: // putchar()
            commit(mode, combine, &opcodes_iterator); // always commit
            mode = PUT_CHAR;
            combine = 0;
            break;
        case GET_CHAR: // getchar()
            commit(mode, combine, &opcodes_iterator);
            mode = GET_CHAR;
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

    // Write the cleanup routine
    write_cleanup_code(&opcodes_iterator);

#ifdef DEBUG // debug files to check real output
    FILE *f = fopen("bf.o", "wb");
    FILE *f2 = fopen("bf.s", "w");

    // raw opcodes
    fwrite(opcodes, 1, opcodes_iterator - opcodes, f);
    // asm file with .byte directives
    fprintf(f2, "        .text\n"
                "        .globl fuck\n"
                "fuck:\n");
    for (size_t i = 0; i < opcodes_iterator - opcodes; i++) {
        fprintf(f2, "        .byte %#02x\n", opcodes[i]);
    }
    fclose(f);
    fclose(f2);
#endif

    // Create our cells with 64 KiB
    const size_t cellsize = 65536;
    unsigned char *cells = (unsigned char *)calloc(1, cellsize);
    if (!cells) {
        printf("Out of memory!\n");
        free(opcodes);
        exit(1);
    }
    const size_t opcodes_len = opcodes_iterator - opcodes;

    run_opcodes(opcodes, opcodes_len, cells);
    dealloc_opcodes(opcodes, memlen);

    free(cells);
}

