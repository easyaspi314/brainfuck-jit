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

/// ARMv5+ JIT code
#ifndef BRAINFUCK_JIT_ARM_H
#define BRAINFUCK_JIT_ARM_H

#if !defined(__arm__) || __ARM_ARCH < 5
#   error "This is for ARMv5+ only! (try changing -march)"
#endif

// strb + blx/subs/adds + ldrb = 12 bytes
#define MAX_INSN_LEN 12
// size of init[]
#define INIT_LEN 20
// size of cleanup[]
#define CLEANUP_LEN 4
// cmp+beq, 8 bytes
#define JUMP_INSN_LEN 8

// Writes the initialization code for our JIT.
static void write_init_code(unsigned char **out)
{
    const unsigned int init[] = {
        // push { r4, r5, r6, lr }
        0xe92d4070,
        // mov r4, r0 // r4 = cells
        0xe1a04000,
        // mov r5, r1 // r5 = getchar
        0xe1a05001,
        // mov r6, r2 // r6 = putchar
        0xe1a06002,
        // ldrb r0, [r4]
        0xe5d40000,
    };
    memcpy(*out, init, sizeof(init));
    *out += sizeof(init);
#ifdef DEBUG
    printf(
        "      .text\n"
        "      .code 32\n"
        "      .globl fuck\n"
        "      .type fuck,%%function\n"
        "fuck:\n"
        "      push    { r4, r5, r6, lr }\n"
        "      mov     r4, r0\n"
        "      mov     r5, r1\n"
        "      mov     r6, r2\n"
        "      ldrb    r0, [r4]\n"
    );
#endif
}

// Writes the cleanup code for our JIT.
static void write_cleanup_code(unsigned char **out)
{
    const unsigned int cleanup[] = {
        // pop { r4, r5, r6, pc }
        0xe8bd8070,
    };
    memcpy(*out, cleanup, sizeof(cleanup));
    *out += sizeof(cleanup);
#ifdef DEBUG
    printf("      pop     { r4, r5, r6, pc }\n");
#endif
}

static inline void commit(int mode, int amount, unsigned char **out_raw)
{
    unsigned int **out = (unsigned int **)out_raw;
    if (mode == 0) return;
    switch (mode) {
    case ADD_DATA:
        if (amount > 0) {
#ifdef DEBUG
            printf("      add     r0, r0, #%i\n", amount & 0xff);
#endif
            *(*out)++ = 0xe2800000 | (amount & 0xff);
        } else if (amount < 0) {
#ifdef DEBUG
            printf("      sub     r0, r0, #%i\n", (-amount) & 0xff);
#endif
            *(*out)++ = 0xe2400000 | ((-amount) & 0xff);
        }
        break;
    case ADD_POINTER:
        if (amount == 0)
            break;
#ifdef DEBUG
        printf("      strb    r0, [r4]\n");
#endif
        *(*out)++ = 0xe5c40000;
        if (amount > 0) {

#ifdef DEBUG
            printf("      add     r4, r4, #%i\n", amount);
#endif
            *(*out)++ = 0xe2844000 | (amount & ((1 << 12) - 1));
        } else {
#ifdef DEBUG
            printf("      sub     r4, r4, #%i\n", -amount);
#endif
            *(*out)++ = 0xe2444000 | ((-amount) & ((1 << 12) - 1));
        }
#ifdef DEBUG
        printf("      ldrb    r0, [r4]\n");
#endif
        *(*out)++ = 0xe5d40000;
        break;
    case PUT_CHAR:
#ifdef DEBUG
        printf("      strb    r0, [r4]\n");
#endif
        *(*out)++ = 0xe5c40000;
#ifdef DEBUG
        printf("      blx     r5\n");
#endif
        *(*out)++ = 0xe12fff35;
#ifdef DEBUG
        printf("      ldrb    r0, [r4]\n");
#endif
        *(*out)++ = 0xe5d40000;
        break;
    case GET_CHAR:
#ifdef DEBUG
        printf("      blx     r6\n");
#endif
        *(*out)++ = 0xe12fff36;
        break;
    case JUMP:
#ifdef DEBUG
        printf("      cmp     r0, #0\n");
#endif
        *(*out)++ = 0xe3500000;
#ifdef DEBUG
        printf("      beq     <tbd>\n");
#endif
        *out += 1; // skip beq
        break;
    case CMP:
#ifdef DEBUG
        printf("      cmp     r0, #0\n");
#endif
        *(*out)++ = 0xe3500000;
#ifdef DEBUG
        printf("      bne     <tbd>\n");
#endif
        *out += 1; // skip bne
        break;
    default: break;
    }
}

static void fill_in_jump(unsigned char *restrict start_raw, unsigned char *restrict opcodes_raw)
{
    unsigned int *start   = (unsigned int *)start_raw;
    unsigned int *opcodes = (unsigned int *)opcodes_raw;
    int diff = start - opcodes - 3;
    // store the address. It is better to use conditionals on ARM; branches take
    // ~3 cycles.
    // bne diff
    opcodes[1] = (diff & 0xFFFFFF) | 0x1a000000;

    // store the jmp address for the loop entry point.
    diff = opcodes - start ;
    // beq diff
    start[-1] = 0x0a000000 | (diff & 0xFFFFFF);
}

static void write_clear_loop(unsigned char *restrict start_raw, unsigned char **restrict out_raw)
{
    *out_raw = start_raw - 8;
    unsigned int **out = (unsigned int **)out_raw;
#ifdef DEBUG
        printf("      mov     r0, #0 @ overwrite\n");
#endif
    *(*out)++ = 0xe3a00000;
}

#endif // BRAINFUCK_JIT_ARM_H
