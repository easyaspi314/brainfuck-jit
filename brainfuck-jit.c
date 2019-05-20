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
static inline void commit(int mode, int amount, unsigned char **out)
{
    switch (mode) {
    case ADD_POINTER:
        if (amount == 0)
            return;

        if (amount == 1) {
#ifdef DEBUG
            printf("        inc     rbx\n");
#endif
            (*out)[0] = 0x48;
            (*out)[1] = 0xff;
            (*out)[2] = 0xc3;
            *out += 3;
        } else if (amount == -1) {
#ifdef DEBUG
            printf("        dec     rbx\n");
#endif
            (*out)[0] = 0x48;
            (*out)[1] = 0xff;
            (*out)[2] = 0xcb;
            *out += 3;
        } else {
#ifdef DEBUG
            printf("        add     rbx, %i\n", amount);
#endif
            (*out)[0] = 0x48;
            (*out)[1] = 0x81;
            (*out)[2] = 0xc3;
            memcpy(*out + 3, &amount, sizeof(int));
            *out += 7;
        }
        return;
    case ADD_DATA:
        if (amount == 0)
            return;

        if (amount == 1) {
#ifdef DEBUG
            printf("        inc     byte ptr[rbx]\n");
#endif
            (*out)[0] = 0xfe;
            (*out)[1] = 0x03;
            *out += 2;
        } else if (amount == -1) {
#ifdef DEBUG
            printf("        dec     byte ptr[rbx]\n");
#endif
            (*out)[0] = 0xfe;
            (*out)[1] = 0x0b;
            *out += 2;
        } else {
#ifdef DEBUG
            printf("        add     byte ptr[rbx], %i\n", amount & 0xFF);
#endif
            (*out)[0] = 0x80;
            (*out)[1] = 0x03;
            (*out)[2] = amount & 0xFF;
            *out += 3;
        }
        return;
    case JUMP: // We jump to the corresponding ] and do the comparison there.
#ifdef DEBUG
        printf("        jmp     <tba>\n");
#endif
        (*out)[0] = 0xe9;
        *out += 5; // placeholder, we insert the address later.
        return;
    case CMP:
#ifdef DEBUG
        printf("        cmp     byte ptr[rbx], 0\n");
#endif
        (*out)[0] = 0x80;
        (*out)[1] = 0x3b;
        (*out)[2] = 0x00;
#ifdef DEBUG
        printf("        jne     <tba>\n");
#endif
        (*out)[3] = 0x0F;
        (*out)[4] = 0x85;
        *out += 9; // placeholder, we insert the address later.
        return;
    case PUT_CHAR:
#ifdef DEBUG
        printf("        movzx   edi, byte ptr[rbx]\n"); // zero extend to int
#endif
        (*out)[0] = 0x0f;
        (*out)[1] = 0xb6;
        (*out)[2] = 0x3b;
#ifdef DEBUG
        printf("        call    r14\n"); // call putchar (which is in rbp)
#endif
        (*out)[3] = 0x41;
        (*out)[4] = 0xff;
        (*out)[5] = 0xd6;
        *out += 6;
        return;
    case GET_CHAR: // Calls getchar (r12), and then stores the result in the address of rbx.
#ifdef DEBUG
        printf("        call    r12\n");
#endif
        (*out)[0] = 0x41;
        (*out)[1] = 0xff;
        (*out)[2] = 0xd4;
#ifdef DEBUG
        printf("        mov     byte ptr[rbx], eax\n");
#endif
        (*out)[3] = 0x88;
        (*out)[4] = 0x03;
        *out += 5;
        return;
    default:
        return;
    }
}

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

    // Map the JIT memory.
    unsigned char *opcodes = (unsigned char *)mmap(
                                 NULL,                               // request a new pointer
                                 9 * len + 128,                      // Account for the worst case, 9 byte opcodes each plus the init routine
                                 PROT_READ | PROT_WRITE | PROT_EXEC, // Read, write, execute. Your fancy malloc can't do that, can you?
                                 MAP_PRIVATE | MAP_ANONYMOUS,        // Private and anonymous. We don't like to share.
                                 -1,                                 // fd: -1 means unused
                                 0                                   // offset: we don't care
                             );
    if (!opcodes) {
        printf("out of memory\n");
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
#ifdef DEBUG
    printf(
        "brainfuck:\n"
        "        push    rbx\n"
        "        push    r12\n"
        "        push    r13\n"
        "        push    r14\n"
        "        mov     r13, rdi\n"
        "        mov     r14, rsi\n"
        "        mov     r12, rdx\n"
        "        sub     rsp, 0x8000\n"
        "        mov     rdi, rsp\n"
        "        xor     esi, esi\n"
        "        mov     edx, 0x8000\n"
        "        call    r13\n"
        "        mov     rbx, rsp\n"
    );
#endif
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
            if (loops_iterator == loops) { // if our stack is empty, fail
                printf("position %zu: Extra ']'n", i);
                free(loops);
                munmap(opcodes, 9 * len + 128);
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
                 // Overwrite the first jmp
                 opcodes_iterator = start - 5;
#ifdef DEBUG
                 printf("        mov     byte ptr[rbx], 0 # overwrite previous\n");
#endif
                 *opcodes_iterator++ = 0xc6;
                 *opcodes_iterator++ = 0x03;
                 *opcodes_iterator++ = 0x00;
                 // Reset the mode
                 mode = 0;
                 combine = 0;
                 // break early
                 break;
            }

            commit(mode, combine, &opcodes_iterator);
            mode = CMP;
            combine = 0;
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
        printf("Position %zu: Missing ]\n", len - 1);
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
#ifdef DEBUG
    printf(
        "        add     rsp, 0x8000\n"
        "        pop     r14\n"
        "        pop     r13\n"
        "        pop     r12\n"
        "        pop     rbx\n"
        "        ret\n"
    );
#endif
    memcpy(opcodes_iterator, cleanup, sizeof(cleanup));

#ifdef DEBUG // debug file to check output
    FILE *f = fopen("bf.o", "wb");
    fwrite(opcodes, 1, (opcodes_iterator + sizeof(cleanup)) - opcodes, f);
    fclose(f);
#endif

    // Now cast to a function pointer (technically nonstandard)...
    brainfuck_t fuck = (brainfuck_t)opcodes;
    // and call with our function pointers to memset, getchar, and putchar.
    // Time to fuck!
    fuck(&memset, &putchar, &getchar);

    // Now cleanup everything and return
    munmap(opcodes, 9 * len + 128);
    free(loops);
}

