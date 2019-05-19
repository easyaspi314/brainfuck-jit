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

#if !defined(__unix__) || defined(_WIN32) || defined(__CYGWIN__)
#error "This is for Unix-based targets only. WSL works, Cygwin or normal Windows does not."
#endif
#ifndef __x86_64__
#error "This JIT code is designed exclusively for x86_64."
#endif

#include <stdio.h> // printf
#include <stdlib.h> // calloc
#include <string.h> // memset, memcpy
#include <sys/mman.h> // mmap

#include "brainfuck-jit.h"

typedef void (*brainfuck_t)(void *(*memset_ptr)(void *, int, size_t), int (*putchar_ptr)(int), int (*getchar_ptr)(void));

// Instruction modes. Subtracting is treated as negative addition.
#define ADD_POINTER '>'
#define SUB_POINTER '<'
#define ADD_DATA '+'
#define SUB_DATA '-'
#define PUT_CHAR '.'
#define GET_CHAR ','
#define JUMP '['
#define CMP ']'

static int commit(int mode, int amount, unsigned char **out)
{
    switch (mode) {
    case ADD_POINTER:
        if (amount == 0)
            return 0;

        if (amount == 1) {
#ifdef DEBUG
            puts("	inc	rbx");
#endif
            *(*out)++ = 0x48;
            *(*out)++ = 0xff;
            *(*out)++ = 0xc3;
            return 3;
        } else if (amount == -1) {
#ifdef DEBUG
            puts("	dec	rbx");
#endif
            *(*out)++ = 0x48;
            *(*out)++ = 0xff;
            *(*out)++ = 0xcb;
            return 3;
        } else if (amount < 0) {
            amount = -amount;
#ifdef DEBUG
            printf("	sub	rbx, %i\n", amount);
#endif
            *(*out)++ = 0x48;
            *(*out)++ = 0x81;
            *(*out)++ = 0xeb;
            memcpy(*out, &amount, sizeof(int));
            *out += sizeof(int);
            return 7;
        } else {
#ifdef DEBUG
            printf("	add	rbx, %i\n", amount);
#endif
            *(*out)++ = 0x48;
            *(*out)++ = 0x81;
            *(*out)++ = 0xc3;
            memcpy(*out, &amount, sizeof(int));
            *out += sizeof(int);
            return 7;
        }
    case ADD_DATA:
        if (amount == 0)
            return 0;

        if (amount == 1) {
#ifdef DEBUG
            puts("	inc	byte ptr[rbx]");
#endif
            *(*out)++ = 0xfe;
            *(*out)++ = 0x03;
            return 2;
        } else if (amount == -1) {
#ifdef DEBUG
            puts("	dec	byte ptr[rbx]");
#endif
            *(*out)++ = 0xfe;
            *(*out)++ = 0x0b;
            return 2;
        } else if (amount < 0) {
#ifdef DEBUG
            printf("	sub	byte ptr[rbx], %i\n", (-amount) % 256);
#endif
            *(*out)++ = 0x80;
            *(*out)++ = 0x2b;
            *(*out)++ = (-amount) % 256;
            return 3;
        } else {
#ifdef DEBUG
            printf("	add	byte ptr[rbx], %i\n", (amount) % 256);
#endif
            *(*out)++ = 0x80;
            *(*out)++ = 0x03;
            *(*out)++ = amount % 256;
            return 3;
        }
    case JUMP: // We jump to the corresponding ] and do the comparison there.
#ifdef DEBUG
        puts("	jmp	<unknown>");
#endif
        *(*out)++ = 0xe9;
        *out += 4; // placeholder, we insert the address later.
        return 5;
    case CMP:
#ifdef DEBUG
        puts("	cmp	byte ptr[rbx], 0");
#endif
        *(*out)++ = 0x80;
        *(*out)++ = 0x3b;
        *(*out)++ = 0x00;
#ifdef DEBUG
        puts("	jne	<unknown>");
#endif
        *(*out)++ = 0x0F;
        *(*out)++ = 0x85;
        *out += 4; // placeholder, we insert the address later.
        return 9;
    case PUT_CHAR:
#ifdef DEBUG
        puts("	movzx	edi, byte ptr[rbx]"); // zero extend to int
#endif
        *(*out)++ = 0x0f;
        *(*out)++ = 0xb6;
        *(*out)++ = 0x3b;
#ifdef DEBUG
        puts("	call	r14"); // call putchar (which is in rbp)
#endif
        *(*out)++ = 0x41;
        *(*out)++ = 0xff;
        *(*out)++ = 0xd6;
        return 6;
    case GET_CHAR: // Calls getchar (r12), and then stores the result in the address of rbx.
#ifdef DEBUG
        puts("	call	r12\n");
#endif
        *(*out)++ = 0x41;
        *(*out)++ = 0xff;
        *(*out)++ = 0xd4;
#ifdef DEBUG
        puts("	mov	byte ptr[rbx], eax");
#endif
        *(*out)++ = 0x88;
        *(*out)++ = 0x03;
        return 5;
    default:
        return 0;
    }
}

void brainfuck(const char *code, size_t len)
{
#ifdef DEBUG
    printf("Optimizing BF code: \"%s\"\n", code);
#endif
    int mode = 0, combine = 0;

    // Our stack to hold loop pointers. We use the worst case scenario in which every char is a loop starter so we don't need to realloc.
    // wesmart
    unsigned char **loops = (unsigned char **)calloc(len, sizeof(unsigned char *));
    if (!loops) {
        puts("out of memory");
        exit(1);
    }
    unsigned char **loops_iterator = loops;

    // Map the JIT memory
    unsigned char *opcodes = (unsigned char *)mmap(
                                 NULL,                               // request a new pointer
                                 9 * len + 128,                      // Account for the worst case, 9 byte opcodes each plus the init routine
                                 PROT_READ | PROT_WRITE | PROT_EXEC, // Read, write, execute. Your fancy malloc can't do that, can you?
                                 MAP_PRIVATE | MAP_ANONYMOUS,        // Private and anonymous. We don't like to share.
                                 -1,                                 // fd: -1 means unused
                                 0                                   // offset: we don't care
                             );
    if (!opcodes) {
        puts("out of memory");
        free(loops);
        exit(1);
    }
    unsigned char *opcodes_iterator = opcodes;

    // void brainfuck(int (*memset_ptr)(void *, int, size_t), int (*putchar_ptr)(int), int (*getchar_ptr)(void));

    // System V calling conventions dictate that the first three parameters are in rdi, rsi, and rdx.
    // Additionally, it also says that rbx, rbp, and r12-r15 are callee saved: In order to use them in your function,
    // you need to push and pop them. We use these registers to store data, as we don't need to worry about pushing and
    // popping in our own code.

    // Initialization routine
    const unsigned char init[] = {
        // Save the callee saved registers.
        // push rbx
        0x53,
        // push r12
        0x41, 0x54,
        // push r13
        0x41, 0x55,
        // push r14
        0x41, 0x56,

        // Save our function pointers into those registers. Otherwise, they will be overwritten
        // mov r13, rdi // r13 = memset
        0x49, 0x89, 0xfd,
        // mov r14, rsi // r14 = putchar
        0x49, 0x89, 0xf6,
        // mov r12, rdx // r12 = getchar
        0x49, 0x89, 0xd4,

        // Set up our stack
        // sub rsp, 0x8000 // get 32768 bytes of stack space, enough for the 30000 required for the specification
        0x48, 0x81, 0xec, 0x00, 0x80, 0x00, 0x00,

        // Set up parameters for memset
        // memset(rsp, 0, 0x8000);
        // mov rdi, rsp // first argument: ptr: stack pointer
        0x48, 0x89, 0xe7,
        // xor esi, esi // second argument: value: 0
        0x31, 0xf6,
        // mov edx, 0x8000 // third argument: length
        0xba, 0x00, 0x80, 0x00, 0x00,

        // Finally, call it (memset is in r13)
        // call r13
        0x41, 0xff, 0xd5,

        // Save our cell pointer (beginning of the stack)
        // mov rbx, rsp - our cell pointer
        0x48, 0x89, 0xe3,
    };

    memcpy(opcodes_iterator, init, sizeof(init));
    opcodes_iterator += sizeof(init);

    for (size_t i = 0; i < len; i++) {
        // We don't actually parse instruction by instruction. We actually delay by at least one instruction
        // so we can join consecutive +- and <>s. commit() will write to the opcodes_iterator.
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
            *loops_iterator++ = opcodes_iterator + 5; // push the address of this opcode to the stack
            break;
        case CMP: { // end loop
            commit(mode, combine, &opcodes_iterator);
            mode = CMP;
            combine = 0;
            if (loops_iterator == loops) { // if our stack is empty, fail
                puts("Extra ]");
                free(loops);
                munmap(opcodes, 9 * len + 128);
                exit(1);
            }
            // Pop from our stack
            unsigned char *start = *--loops_iterator;
            // The difference between the instruction after jne and the beginning of the loop
            int diff = start - opcodes_iterator - 9;
            // store the jne address
            memcpy(opcodes_iterator + 5, &diff, sizeof(int));
            // store the jmp address for the loop entry point.
            diff = opcodes_iterator - start;
            memcpy(start - 4, &diff, sizeof(int));
            break;
        }
        case PUT_CHAR: // putchar()
            commit(mode, combine, &opcodes_iterator);
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
        puts("Missing ]");
        munmap(opcodes, 9 * len + 128);
        free(loops);
        exit(1);
    }

    // Last call to commit() to handle the final instruction
    commit(mode, combine, &opcodes_iterator);

    // Clean up code: Restores the stack, pops registers, and returns.
    const unsigned char cleanup[] = {
        // add rsp, 0x8000 - free the stack memory
        0x48, 0x81, 0xc4, 0x00, 0x80, 0x00, 0x00,
        // pop r14
        0x41, 0x5e,
        // pop r13
        0x41, 0x5d,
        // pop r12
        0x41, 0x5c,
        // pop rbx
        0x5b,
        // ret
        0xc3
    };

    memcpy(opcodes_iterator, cleanup, sizeof(cleanup));

#ifdef DEBUG // debug file to check output
    FILE *f = fopen("bf.o", "wb");
    fwrite(opcodes, 1, (opcodes_iterator + sizeof(cleanup)) - opcodes, f);
    fclose(f);
#endif

    // now cast to a function pointer
    brainfuck_t fuck = (brainfuck_t)opcodes;
    // and call with our function pointers to memset, getchar, and putchar
    fuck(&memset, &putchar, &getchar);

    // Now cleanup
    munmap(opcodes, 9 * len + 128);
    free(loops);
}

