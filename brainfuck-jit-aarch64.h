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

/// Aarch64 JIT code
#ifndef BRAINFUCK_JIT_AARCH64_H
#define BRAINFUCK_JIT_AARCH64_H

#if !defined(__arm64__) && !defined(__aarch64__) && !defined(_M_ARM64)
#   error "This is for aarch64 only!"
#endif

// sub + strb + blx/subs/adds + ldrb = 20 bytes
#define MAX_INSN_LEN 20
// size of init[]
#define INIT_LEN 24
// size of cleanup[]
#define CLEANUP_LEN 12

typedef uint32_t raw_opcode;

// Writes the initialization code for our JIT.
static void write_init_code(uint32_t *restrict out, size_t *restrict pos)
{
    const uint32_t init[] = {
        // push x19, x20, x21, and lr (x30) to the stack
        // stp x19, x20, [sp, #-16]!
        0xa9bf53f3,
        // stp x21, x30, [sp, #-16]!
        0xa9bf7bf5,

        // mov x19, w0 // x19 = cells
        0xaa0003f3,
        // mov x20, x1 // x20 = getchar
        0xaa0103f4,
        // mov x21, r2 // x21 = putchar
        0xaa0203f5,
        // mov w0, #0
        0x52800000,
    };
    memcpy(out + (*pos), init, sizeof(init));
    *pos += sizeof(init) / sizeof(uint32_t);
    bf_log(
        "      .text\n"
        "      .globl fuck\n"
        "      .type fuck,%%function\n"
        "fuck:\n"
        "      stp     x19, x20, [sp, #-16]!\n"
        "      stp     x21, x30, [sp, #-16]!\n"
        "      mov     x19, x0 // x19 = cells\n"
        "      mov     x20, x1 // x20 = &getchar\n"
        "      mov     x21, x2 // x21 = &putchar\n"
        "      mov     w0, #0 // start with an initial zero\n"
    );
}

// Writes the cleanup code for our JIT.
static void write_cleanup_code(uint32_t *restrict out, size_t *restrict pos)
{
    const uint32_t cleanup[] = {
        // ldp x21, x30, [sp], #16
        0xa8c17bf5,
        // ldp x19, x20, [sp], #16
        0xa8c153f3,
        // ret
        0xd65f03c0,
    };
    memcpy(out + (*pos), cleanup, sizeof(cleanup));
    *pos += sizeof(cleanup) / sizeof(uint32_t);
    bf_log(
        "      ldp     x21, x30, [sp], #16\n"
        "      ldp     x19, x20, [sp], #16\n"
        "      ret\n"
    );
}

// Returns the opcode for add x1, x1, w0, lsl #log2(val&0xff) if val & 0xff
// is a power of 2, or zero if it isn't.
//
// add x1, x1, w0, lsl #2
// x1 = x1 + (w0 << 2);
static inline uint32_t get_shift_add_insn(int32_t val)
{
    switch (val) {
        case -1: // -1 - negate
            bf_log("      sub     w1, w1, w0\n");
            return 0x4b000021;
        case -2:
            bf_log("      sub     w1, w1, w0, lsl #1\n");
            return 0x4b000421;
        case -4:
            bf_log("      sub     w1, w1, w0, lsl #2\n");
            return 0x4b000821;
        case -8:
            bf_log("      sub     w1, w1, w0, lsl #3\n");
            return 0x4b000c21;
        case -16: // 16
            bf_log("      sub     w1, w1, w0, lsl #4\n");
            return 0x4b001021;
        case -32: // 32
            bf_log("      sub     w1, w1, w0, lsl #5\n");
            return 0x4b001421;
        case -64: // 64
            bf_log("      sub     w1, w1, w0, lsl #6\n");
            return 0x4b001821;

        case 1 << 0: // 1 - normal add insn
            bf_log("      add     w1, w1, w0\n");
            return 0x0b000021;
        case 1 << 1: // 2
            bf_log("      add     w1, w1, w0, lsl #1\n");
            return 0x0b000421;
        case 1 << 2: // 4
            bf_log("      add     w1, w1, w0, lsl #2\n");
            return 0x0b000821;
        case 1 << 3: // 8
            bf_log("      add     w1, w1, w0, lsl #3\n");
            return 0x0b000c21;
        case 1 << 4: // 16
            bf_log("      add     w1, w1, w0, lsl #4\n");
            return 0x0b001021;
        case 1 << 5: // 32
            bf_log("      add     w1, w1, w0, lsl #5\n");
            return 0x0b001421;
        case 1 << 6: // 64
            bf_log("      add     w1, w1, w0, lsl #6\n");
            return 0x0b001821;
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
            bf_log("      add     w0, w0, #%i\n", opcode->amount & 0xff);
            out[(*pos)++] = 0x11000000 | ((opcode->amount & 0xff) << 10);
        } else if (opcode->amount < 0) {
            bf_log("      sub     w0, w0, #%i\n", (-opcode->amount) & 0xff);
            out[(*pos)++] = 0x51000000 | ((-opcode->amount & 0xff) << 10);
        }
        break;
    case bf_opcode_move:
        if (opcode->amount == 0)
            break;
        bf_log("      strb    w0, [x19]\n");
        // Save our working copy
        out[(*pos)++] = 0x39000260;
        // rare but possible
        if (opcode->amount > 255 || opcode->amount < -256) {
            if (opcode->amount > 0) {
                bf_log("     add     x19, x19, #%i\n", opcode->amount);
                out[(*pos)++] = 0x91000273 | ((opcode->amount & 0xFFF) << 10);
            } else {
                bf_log("     sub     x19, x19, #%i\n", -opcode->amount);
                out[(*pos)++] = 0xd1000273 | ((-opcode->amount & 0xFFF) << 10);
            }
            bf_log("      ldrb    w0, [x19]\n");
            out[(*pos)++] = 0x39400260;
        } else {
            bf_log("      ldrb    w0, [x19, #%i]!\n", opcode->amount);
            out[(*pos)++] = 0x38400e60 | ((opcode->amount & ((1 << 9) - 1)) << 12);
        }
        break;
    case bf_opcode_put:
        // Avoid redundant store
        if (*pos < 1 + INIT_LEN / 4 || opcode[-1].op != bf_opcode_move) {
            bf_log("      strb    w0, [x19]\n");
            out[(*pos)++] = 0x39000260;
        }
        bf_log("      blr     x20\n");
        out[(*pos)++] = 0xd63f0280;
        bf_log("      ldrb    w0, [x19]\n");
        out[(*pos)++] = 0x39400260;
        break;
    case bf_opcode_get:
        bf_log("      blr     x21\n");
        out[(*pos)++] = 0xd63f02a0;
        break;
    case bf_opcode_start:
        bf_log("      tst     w0, #0xFF\n");
        out[(*pos)++] = 0x72001c1f;
        bf_log("      b.eq    <tbd>\n");

        *pos += 1; // skip beq
        opcode->amount = (int32_t)(*pos);
        break;
    case bf_opcode_end: {
        bf_log("      tst     w0, #0xFF\n");
        out[(*pos)++] = 0x72001c1f;
        bf_opcode *start = &opcode[opcode->amount];
        int32_t offset_to = start->amount - (*pos);
        int32_t offset_from = 2 + (*pos) - (start->amount);
        // b.eq
        out[start->amount - 1] = 0x54000000 | ((offset_from & ((1<<19)-1)) << 5);
        bf_log("      b.ne    <tbd>\n");
        out[(*pos)++] = 0x54000001 | ((offset_to & ((1<<19)-1)) << 5);

        break;
    }
    case bf_opcode_clear:
        bf_log("      mov     w0, #0\n");
        out[(*pos)++] = 0x52800000;
        break;
    case bf_opcode_copy_mul: {
        // If we are multiplying by zero we ignore it.
        if ((opcode->amount & 0xFF) == 0 || (opcode->amount >> 8) == 0) // nop
            break;
        // sign extend
        int32_t amount = (int8_t)opcode->amount;

        // If we have a power of 2, we do this:
        //    ldrb    x1, [x19, #offset]
        //    add     x1, x1, w0, lsl #shift
        //    strb    x1, [x19, #offset]
        //
        // Otherwise we do this:
        //    ldrb    x1, [x19, #offset]
        //    mov     r2, #amt
        //    mla     x1, w0, r2, x1
        //    strb    x1, [x19, #offset]

        // strb doesn't allow negative offsets without !, yet str does?
        if (opcode->amount >> 8 < 0) {
            bf_log("      sub     x3, x19, #%i\n", (-opcode->amount) >> 8);
            out[(*pos)++] = 0xd1000263 | ((-(opcode->amount >> 8) & 0xfff) << 10);
            bf_log("      ldrb    w1, [x3]\n");
            out[(*pos)++] = 0x39400061;
        } else {
            bf_log("      ldrb    w1, [x19, #%i]\n", opcode->amount >> 8);
            // holy shift!
            out[(*pos)++] = 0x39400261 | (((opcode->amount >> 8) & ((1 << 9) - 1)) << 10);
        }

        // either 0 or the opcode we need
        uint32_t shift_insn = get_shift_add_insn(amount);

        if (shift_insn == 0) { // not a power of 2, do it out
            bf_log("      mov     w2, #%i\n", amount);
            if (amount < 0) {
                out[(*pos)++] = 0x12800002 | ((~amount & 0xFFFF) << 5);
            } else {
                out[(*pos)++] = 0x52800002 | ((amount & 0xFFFF) << 5);
            }
            // x1 = w0 * w2 + x1;
            bf_log("      madd    w1, w0, w2, w1\n");
            out[(*pos)++] = 0x1b020401;
        } else {
            // shift_insn logs
            out[(*pos)++] = shift_insn;
        }

        if (opcode->amount >> 8 < 0) {
            bf_log("      strb    w1, [x3]\n");
            out[(*pos)++] = 0x39000061;
        } else {
            bf_log("      strb    w1, [x19, #%i]\n", opcode->amount >> 8);
            out[(*pos)++] = 0x39000261 | (((opcode->amount >> 8) & ((1 << 9) - 1)) << 10);
        }
        break;
    }
    default:
        break;
    }
}


#endif // BRAINFUCK_JIT_ARM_H
