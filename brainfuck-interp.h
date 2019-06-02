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


/// brainfuck-interp.h: Portable, non-JIT interpreter. Still has decent performance.
#ifndef BRAINFUCK_INTERP_H
#define BRAINFUCK_INTERP_H

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
#define bf_opcode_ext_shl_add '{'
#define bf_opcode_ext_shl_sub '}'
#define bf_opcode_ext_copy '"'
#define bf_opcode_ext_mul 'x'
#define bf_opcode_ext_inc 'i'
#define bf_opcode_ext_dec 'd'
#define bf_opcode_ext_add 'A'
#define bf_opcode_ext_inc_move 'I'
#define bf_opcode_ext_dec_move 'D'
#define bf_opcode_ext_move 'M'

static inline int log_2(int val) {
    switch (val) {
    case 1: return 0;
    case -1: return 0;
    case 2: return 1;
    case -2: return 1;
    case 4: return 2;
    case -4: return 2;
    case 8: return 3;
    case -8: return 3;
    case 16: return 4;
    case -16: return 4;
    case 32: return 5;
    case -32: return 5;
    case 64: return 6;
    case -64: return 6;
    default: return 0;
    }
}

// Interprets our pre-parsed format.
static void run_opcodes(bf_opcode *restrict opcodes, size_t len)
{
    uint8_t *cells = (uint8_t *)calloc(1, 65536);
    if (!cells) {
        printf("Out of memory\n");
        exit(1);
    }
    uint8_t *cell = cells;
    int32_t amount = 0, offset = 0, temp = 0;
    bf_opcode *op = opcodes, *end = opcodes + len;
    // TODO: update debug logs
    while (op < end) {
        switch (op->op) {
        case bf_opcode_move:
            if (op->amount == 1) {
                op->op = bf_opcode_ext_inc_move;
                ++cell;
                break;
            }
            if (op->amount == -1) {
                op->op = bf_opcode_ext_dec_move;
                --cell;
                break;
            }
            op->op = bf_opcode_ext_move;
            cell += op->amount;
            break;
        case bf_opcode_ext_move:
            bf_log("cell += %d;\n", op->amount);
            cell += op->amount;
            break;
        case bf_opcode_ext_dec_move:
            --cell;
            break;
        case bf_opcode_ext_inc_move:
            ++cell;
            break;

        case bf_opcode_add:
            if (op->amount == 1) {
                op->op = bf_opcode_ext_inc;
                ++*cell;
                break;
            }
            if (op->amount == -1) {
                op->op = bf_opcode_ext_dec;
                --*cell;
                break;
            }
            op->op = bf_opcode_ext_add;
            *cell += op->amount;
            break;
        case bf_opcode_ext_add:
            bf_log("*cell += %d;\n", op->amount);
            *cell += op->amount;
            break;
        case bf_opcode_ext_dec:
            --*cell;
            break;
        case bf_opcode_ext_inc:
            ++*cell;
            break;
        case bf_opcode_put:
            bf_log("putchar(%d /* '%c' */);\n", *cell, *cell);
            putc(*cell, stdout);
            break;
        case bf_opcode_get:
            *cell = getc(stdin);
            bf_log("*cell = getchar(); /* %i */;\n", *cell);
            break;
        case bf_opcode_clear:
            bf_log("*cell = 0;\n");
            *cell = 0;
            break;
        case bf_opcode_start:
            bf_log("if (%i == 0) {\n i += %i;\n}\n", *cell, op->amount);
            if (*cell == 0) {
                op += op->amount;
            }
            break;
        case bf_opcode_end:
            bf_log("if (%i != 0) {\n    i += %i;\n}\n", *cell, op->amount);
            if (*cell != 0) {
                op += op->amount;
            }
            break;
        // Split up the opcode using some sneaky gotos
        case bf_opcode_copy_mul:
            offset = op->amount >> 8;
            amount = (int8_t)op->amount;
            if (amount == 0) {
                op->op = bf_opcode_nop;
                break;
            } else if (amount == 1) {
                op->op = bf_opcode_ext_copy;
                op->amount >>= 8;
                cell[offset] += *cell;
                break;
            } else if ((temp = log_2(amount))) {
                op->amount = (offset << 8) | temp;
                if (amount < 0) {
                    op->op = bf_opcode_ext_shl_sub;
                    cell[offset] -= *cell << temp;
                } else {
                    op->op = bf_opcode_ext_shl_add;
                    cell[offset] += *cell << temp;
                }
                break;
            } else {
                op->op = bf_opcode_ext_mul;
                cell[offset] += amount * *cell;
                break;
            }
            break;
        case bf_opcode_ext_mul:
            offset = op->amount >> 8;
            amount = (int8_t)op->amount;
            bf_log("cells[%i] += %i * cells[0];\n", offset, amount);
            cell[offset] += amount * *cell;
            break;
        case bf_opcode_ext_copy:
            cell[op->amount] += *cell;
            break;
        case bf_opcode_ext_shl_add:
            cell[op->amount>>8] += *cell << (op->amount & 0xFF);
            break;
        case bf_opcode_ext_shl_sub:
            cell[op->amount>>8] -= *cell << (op->amount & 0xFF);
            break;
        default:
            break;
       }
       ++op;
   }
   free(cells);
}

#endif // BRAINFUCK_INTERP_H

