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
#ifndef BRAINFUCK_JIT_ARM_H
#define BRAINFUCK_JIT_ARM_H

#if !defined(__arm__) || __ARM_ARCH < 5
#   error "This is for ARMv5+ only! (try changing -march)"
#endif

// cmp + jne = 9 bytes
#define MAX_INSN_LEN 12
// size of init[]
#define INIT_LEN 20
// size of cleanup[]
#define CLEANUP_LEN 4
// jmp address = 5 bytes
#define JUMP_INSN_LEN 8

/*

1420:       b570            push    {r4, r5, r6, lr}
    1422:       0004            movs    r4, r0
    1424:       000d            movs    r5, r1
    1426:       0016            movs    r6, r2
    1428:       3c03            subs    r4, #3
    142a:       7820            ldrb    r0, [r4, #0]
    142c:       3004            adds    r0, #4
    142e:       7020            strb    r0, [r4, #0]
    1430:       47a8            blx     r5
    1432:       47b0            blx     r6
    1434:       f000 b801       b.w     143a <main+0x1a>
    1438:       3001            adds    r0, #1
    143a:       2800            cmp     r0, #0
    143c:       f47f affc       bne.w   1438 <main+0x18>
    1440:       bd70            pop     {r4, r5, r6, pc}
    1442:       4770            bx      lr
00000000 <main>:
   0:   e92d4070        push    {r4, r5, r6, lr}
   4:   e1a04000        mov     r4, r0
   8:   e1a05001        mov     r5, r1
   c:   e1a06002        mov     r6, r2
  10:   e3a00000        mov     r0, #0
  14:   e2444003        sub     r4, r4, #3
  18:   e5d40000        ldrb    r0, [r4]
  1c:   e2800004        add     r0, r0, #4
  20:   e5c40000        strb    r0, [r4]
  24:   e12fff35        blx     r5
  28:   e12fff36        blx     r6
  2c:   0a000000        beq     34 <main+0x34>
  30:   e2900001        adds    r0, r0, #1
  34:   e3500000        cmp     r0, #0
  38:   1afffffc        bne     30 <main+0x30>
  3c:   e8bd8070        pop     {r4, r5, r6, pc}
  40:   e12fff1e        bx      lr
*/
// Writes the initialization code for our JIT.
static void write_init_code(unsigned char **out)
{
    const unsigned int init[] = {
        // push { r4, r5, r6, lr }
        0xe92d4070,
        // movs r4, r0 // r4 = cells
        0xe1a04000,
        // movs r5, r1 // r5 = getchar
        0xe1a05001,
        // movs r6, r2 // r6 = putchar
        0xe1a06002,
        // ldrb r0, [r4]
        0xe5d40000,
    };
    memcpy(*out, init, sizeof(init));
    *out += sizeof(init);
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
}

static void commit(int mode, int amount, unsigned char **out_raw)
{
    unsigned int **out = (unsigned int **)out_raw;
    if (mode == 0) return;
    switch (mode) {
    case ADD_DATA:
        if (amount > 0) {
            // add r0, r0, #amt
            *(*out)++ = 0xe2800000 | (amount & ((1 << 12) - 1));
        } else if (amount < 0) {
            // sub r0, r0, #amt
            *(*out)++ = 0xe2400000 | ((-amount) & ((1 << 12) - 1));
        }
        break;
    case ADD_POINTER:
        if (amount == 0)
            break;
        // strb r0, [r4]
        *(*out)++ = 0xe5c40000;
        if (amount > 0) {
            // adds r4, #amt
            *(*out)++ = 0xe2844000 | (amount & ((1 << 12) - 1));
        } else {
            // subs r4, #amt
            *(*out)++ = 0xe2444000 | ((-amount) & ((1 << 12) - 1));
        }
        // ldrb r0, [r4]
        *(*out)++ = 0xe5d40000;
        break;
    case PUT_CHAR:
        // strb r0, [r4]
        *(*out)++ = 0xe5c40000;
        // blx r5
        *(*out)++ = 0xe12fff35;
        // ldrb r0, [r4]
        *(*out)++ = 0xe5d40000;
        break;
    case GET_CHAR:
        // blx r6
        *(*out)++ = 0xe12fff36;
        break;
    case JUMP:
       // cmp r0, #0
       *(*out)++ = 0xe3500000;
       // beq <tbd>
       (*out_raw)[3] = 0x0a;
       *out += 4;
       break;
    case CMP:
       *(*out)++ = 0xe3500000;
       // bne <tbd>
       (*out_raw)[3] = 0x1a;
       *out += 4;
       break;
    default: break;
    }
}
static void fill_in_jump(unsigned char *restrict start_raw, unsigned char *restrict opcodes_raw)
{
    unsigned int *start   = (unsigned int *)start_raw;
    unsigned int *opcodes = (unsigned int *)opcodes_raw;
    int diff = start - opcodes - 2;
    // store the jne address
    unsigned int tmp = (unsigned)opcodes_raw[7] << 24;
    opcodes[1] = (diff & 0xFFFFFF) | tmp;

    // store the jmp address for the loop entry point.
    diff = opcodes - start;
    tmp = (unsigned)start_raw[-1] << 24;
    start[-1] = (diff & 0xFFFFFF) | tmp;
}
static void write_clear_loop(unsigned char *restrict start_raw, unsigned char **restrict out_raw)
{
    unsigned int **out = (unsigned int **)out_raw;
    *out = ((unsigned int *)start_raw) - 8;
    // mov r0, #0
    *(*out)++ = 0xe3a00000;
}


#endif // BRAINFUCK_JIT_THUMB_H
