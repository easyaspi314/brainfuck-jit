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
#define MAX_INSN_LEN 16
// size of init[]
#define INIT_LEN 20
// size of cleanup[]
#define CLEANUP_LEN 4
// cmp+beq, 8 bytes
#define JUMP_INSN_LEN 8

typedef uint32_t raw_opcode;

// Writes the initialization code for our JIT.
static void write_init_code(uint32_t *restrict out, size_t *restrict pos)
{
    const uint32_t init[] = {
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
    memcpy(out + (*pos), init, sizeof(init));
    *pos += sizeof(init) / sizeof(uint32_t);
    bf_log(
        "      .text\n"
        "      .code 32\n"
        "      .globl fuck\n"
        "      .type fuck,%%function\n"
        "fuck:\n"
        "      push    { r4, r5, r6, lr }\n"
        "      mov     r4, r0 @ r4 = cells\n"
        "      mov     r5, r1 @ r5 = &getchar\n"
        "      mov     r6, r2 @ r6 = &putchar\n"
        "      mov     r0, #0 @ start with an initial zero\n"
    );
}

// Writes the cleanup code for our JIT.
static void write_cleanup_code(uint32_t *restrict out, size_t *restrict pos)
{
    const uint32_t cleanup[] = {
        // pop { r4, r5, r6, pc }
        0xe8bd8070,
    };
    memcpy(out + (*pos), cleanup, sizeof(cleanup));
    *pos += sizeof(cleanup) / sizeof(uint32_t);
    bf_log("      pop     { r4, r5, r6, pc }\n");
}

// Returns the opcode for add r1, r1, r0, lsl #log2(val&0xff) if val & 0xff
// is a power of 2, or zero if it isn't.
//
// add r1, r1, r0, lsl #2
// r1 = r1 + (r0 << 2);
static inline uint32_t get_shift_add_insn(int32_t val)
{
    switch (val & 0xFF) {
        case 1 << 0: // 1 - normal add insn
            bf_log("      add     r1, r1, r0\n");
            return 0xe0811000;
        case 1 << 1: // 2
            bf_log("      add     r1, r1, r0, lsl #1\n");
            return 0xe0811080;
        case 1 << 2: // 4
            bf_log("      add     r1, r1, r0, lsl #2\n");
            return 0xe0811100;
        case 1 << 3: // 8
            bf_log("      add     r1, r1, r0, lsl #3\n");
            return 0xe0811180;
        case 1 << 4: // 16
            bf_log("      add     r1, r1, r0, lsl #4\n");
            return 0xe0811200;
        case 1 << 5: // 32
            bf_log("      add     r1, r1, r0, lsl #5\n");
            return 0xe0811280;
        case 1 << 6: // 64
            bf_log("      add     r1, r1, r0, lsl #6\n");
            return 0xe0811300;
        case 1 << 7: // 128
            bf_log("      add     r1, r1, r0, lsl #7\n");
            return 0xe0811380;
        default:
            return 0;
    }
}

static void compile_opcode(bf_opcode *restrict opcode, uint32_t *restrict out, size_t *restrict pos)
{
    if (opcode->op == bf_opcode_nop)
        return;
    switch (opcode->op) {
    case bf_opcode_add:
        if (opcode->amount > 0) {
            bf_log("      add     r0, r0, #%i\n", opcode->amount & 0xff);
            out[(*pos)++] = 0xe2800000 | (opcode->amount & 0xff);
        } else if (opcode->amount < 0) {
            bf_log("      sub     r0, r0, #%i\n", (-opcode->amount) & 0xff);
            out[(*pos)++] = 0xe2400000 | ((-opcode->amount) & 0xff);
        }
        break;
    case bf_opcode_move:
        if (opcode->amount == 0)
            break;
        bf_log("      strb    r0, [r4]\n");
        // Save our working copy
        out[(*pos)++] = 0xe5c40000;
        bf_log("      ldrb    r0, [r4, #%i]!\n", opcode->amount);
        if (opcode->amount > 0) {

            out[(*pos)++] = 0xe5f40000 | (opcode->amount & ((1 << 12) - 1));
        } else {
            out[(*pos)++] = 0xe5740000 | ((-opcode->amount) & ((1 << 12) - 1));
        }
        break;
    case bf_opcode_put:
        bf_log("      strb    r0, [r4]\n");
        out[(*pos)++] = 0xe5c40000;

        bf_log("      blx     r5\n");
        out[(*pos)++] = 0xe12fff35;

        bf_log("      ldrb    r0, [r4]\n");
        out[(*pos)++] = 0xe5d40000;
        break;
    case bf_opcode_get:
        bf_log("      blx     r6\n");
        out[(*pos)++] = 0xe12fff36;
        break;
    case bf_opcode_start:
        bf_log("      tst     r0, #0xFF\n");
        out[(*pos)++] = 0xe31000ff;

        bf_log("      beq     <tbd>\n");

        *pos += 1; // skip beq
        opcode->amount = (int32_t)(*pos);
        break;
    case bf_opcode_end: {

        bf_log("      tst     r0, #0xFF\n");
        out[(*pos)++] = 0xe31000ff;
        bf_opcode *start = &opcode[opcode->amount];
        int32_t offset_to = start->amount - (*pos + 2);
        int32_t offset_from = (*pos) - (start->amount);
        out[start->amount - 1] = 0x0a000000 | (offset_from & 0xFFFFFF);
        bf_log("      bne     <tbd>\n");
        out[(*pos)++] = (offset_to & 0xFFFFFF) | 0x1a000000;

        break;
    }
    case bf_opcode_clear:
        bf_log("      mov     r0, #0\n");
        out[(*pos)++] = 0xe3a00000;
        break;
    case bf_opcode_copy_mul: {
        // If we are multiplying by zero we ignore it.
        if ((opcode->amount & 0xFF) == 0 || (opcode->amount >> 8) == 0) // nop
            break;
        // If we have a power of 2, we do this:
        //    ldrb    r1, [r4, #offset]
        //    add     r1, r1, r0, lsl #shift
        //    strb    r1, [r4, #offset]
        //
        // Otherwise we do this:
        //    ldrb    r1, [r4, #offset]
        //    mov     r2, #amt
        //    mla     r1, r0, r2, r1
        //    strb    r1, [r4, #offset]

        bf_log("      ldrb    r1, [r4, #%i]\n", opcode->amount >> 8);
        out[(*pos)++] = 0xe5d41000 | ((opcode->amount >> 8) & 4095);

        // either 0 or the opcode we need
        uint32_t shift_insn = get_shift_add_insn(opcode->amount);

        if (shift_insn == 0) { // not power of 2
            bf_log("      mov     r2, #%i\n", opcode->amount & 0xff);
            out[(*pos)++] = 0xe3a02000 | (opcode->amount & 0xFF);

            // r1 = r0 * r2 + r1;
            bf_log("      mla     r1, r0, r2, r1\n");
            out[(*pos)++] = 0xe0211290;
        } else {
            // shift_insn logs
            out[(*pos)++] = shift_insn;
        }

        bf_log("      strb    r1, [r4, #%i]\n", opcode->amount >> 8);
        out[(*pos)++] = 0xe5c41000 | ((opcode->amount >> 8) & 4095);
        break;
    }
    default:
        break;
    }
}


#endif // BRAINFUCK_JIT_ARM_H
