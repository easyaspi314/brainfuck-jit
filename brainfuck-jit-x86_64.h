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
#ifndef BRAINFUCK_JIT_X86_64_H
#define BRAINFUCK_JIT_X86_64_H

#if !(defined(__x86_64__) || defined(_M_X64) || defined(__amd64__) || defined(_M_AMD64))
#  error "This JIT code is designed exclusively for x86_64."
#endif
#ifndef BRAINFUCK_JIT_C
#  error "This file is only to be included from brainfuck-jit.c"
#endif
#include <string.h>

// cmp + jne = 9 bytes
#define MAX_INSN_LEN 9
// size of init[]
#define INIT_LEN 14
// size of cleanup[]
#define CLEANUP_LEN 6
// jmp address = 5 bytes
#define JUMP_INSN_LEN 5

// Writes the initialization code for our JIT.
static void write_init_code(unsigned char **out)
{

    // System V calling conventions dictate that the first three parameters are in rdi, rsi, and rdx.
    // Windows calling conventions say that the first three parameters are rcx, rdx, and r8.
    //
    // Additionally, they both say that rbx, rbp, and r12-r15 are callee saved: In order to use them in your function,
    // you need to push and pop them. We use these registers to store data, as we don't need to worry about pushing and
    // popping in our own code.

    // Initialization routine
    const unsigned char init[] = {
        // Save the callee saved registers.
        // push rbx
        0x53,
        // push r12
        0x41, 0x54,
        // push r14
        0x41, 0x56,

#ifdef _WIN32 // Windows ABI
        // Save our function pointers into those registers. Otherwise, they will be overwritten
        // when we call putchar/getchar. We also move the cells pointer around.
        // mov rbx, rcx // rbx = cells
        0x48, 0x89, 0xcb,
        // mov r14, rdx // r14 = putchar
        0x49, 0x89, 0xd6,
        // mov r12, r8 // r12 = getchar
        0x4d, 0x89, 0xc4,

#else // System V ABI
        // Save our function pointers into those registers. Otherwise, they will be overwritten
        // when we call putchar/getchar. We also move the cells pointer around.
        // mov rbx, rdi // rdi = cells
        0x48, 0x89, 0xfb,
        // mov r14, rsi // r14 = putchar
        0x49, 0x89, 0xf6,
        // mov r12, rdx // r12 = getchar
        0x49, 0x89, 0xd4,
#endif // !_WIN32
    };
#ifdef DEBUG
    printf(
        "fuck:\n"
        "        push    rbx\n"
        "        push    r12\n"
        "        push    r14\n"
#ifdef _WIN32
        "        mov     rbx, rcx\n"
        "        mov     r14, rdx\n"
        "        mov     r12, r8\n"
#else
        "        mov     rbx, rdi\n"
        "        mov     r14, rsi\n"
        "        mov     r12, rdx\n"
#endif
    );
#endif
    memcpy(*out, init, sizeof(init));
    *out += sizeof(init);
}

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
#ifdef _WIN32 // Windows ABI
#  ifdef DEBUG
        printf("        movzx   ecx, byte ptr[rbx]\n"); // zero extend to int
#  endif
        *(*out)++ = 0x0f;
        *(*out)++ = 0xb6;
        *(*out)++ = 0x0b;
#else // System V ABI
#  ifdef DEBUG
        printf("        movzx   edi, byte ptr[rbx]\n"); // zero extend to int
#  endif
        *(*out)++ = 0x0f;
        *(*out)++ = 0xb6;
        *(*out)++ = 0x3b;
#endif

#ifdef DEBUG
        printf("        call    r14\n"); // call putchar (which is in rbp)
#endif
        *(*out)++ = 0x41;
        *(*out)++ = 0xff;
        *(*out)++ = 0xd6;
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



// Writes the cleanup code for our JIT.
static void write_cleanup_code(unsigned char **out)
{

    // Clean up code: Restores the stack, pops registers, and returns.
    const unsigned char cleanup[] = {
        // pop r14
        0x41, 0x5e,
        // pop r12
        0x41, 0x5c,
        // pop rbx
        0x5b,
        // ret
        0xc3
    };
    memcpy(*out, cleanup, sizeof(cleanup));
    *out += sizeof(cleanup);

#ifdef DEBUG
    printf(
        "        pop     r14\n"
        "        pop     r12\n"
        "        pop     rbx\n"
        "        ret\n"
    );
#endif
}


// Fills in a jump block
static void fill_in_jump(unsigned char *restrict start, unsigned char *restrict opcodes_iterator)
{

    // The difference between the instruction after jne and the beginning of the loop
    int diff = start - opcodes_iterator - 9;
    // store the jne address
    memcpy(opcodes_iterator + 5, &diff, sizeof(int));
    // store the jmp address for the loop entry point.
    diff = opcodes_iterator - start;
    memcpy(start - 4, &diff, sizeof(int));
}

// Overwrites a jump with a clear loop.
static void write_clear_loop(unsigned char *restrict start, unsigned char **restrict out)
{
     // Overwrite the first jmp
     *out = start - 5;
#ifdef DEBUG
     printf("        mov     byte ptr[rbx], 0 # overwrite previous\n");
#endif
     *(*out)++ = 0xc6;
     *(*out)++ = 0x03;
     *(*out)++ = 0x00;
}

#endif // BRAINFUCK_JIT_X86_64_H
