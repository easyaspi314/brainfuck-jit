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

#ifndef BRAINFUCK_IR_H
#define BRAINFUCK_IR_H

#ifndef BRAINFUCK_JIT_C
#   error "Do not include this file directly!"
#endif

/// Optimization: Converts [->++<] to cell[1] += cell[0] * 2;
/// We do this by evaluating the loop and checking the result.
/// Returns the number of instructions overwritten.
static int update_copyloop(bf_opcode *restrict program, size_t size)
{
    // Copy/multiply loop must start with a decrement
    if (program[1].op != bf_opcode_add || program[1].amount > 0) {
        bf_log("not a copy loop\n");
        return 0;
    }
#define MAX_OFFSET 30
    int mult = 0;
    int offset = 0;
    int mults[MAX_OFFSET] = {0};
    int offsets[MAX_OFFSET] = {0};

    size_t nmults = 0;
    size_t i = 2; // skip [-

    while (i <= size && program[i].op != bf_opcode_end && nmults < MAX_OFFSET) {
        if (program[i].op == bf_opcode_move) {
            // working product, store it
            if (mult != 0) {
                mults[nmults] = mult;
                offsets[nmults] = offset;
                mult = 0;
                ++nmults;
            }
            offset += program[i].amount;
        } else if (program[i].op == bf_opcode_add) {
            mult += program[i].amount;
        } else if (program[i].op != bf_opcode_nop) { // non-nops are a show stopper
            return 0;
        }
        ++i;
    }

    // Either the loop is too large or there are no multiplies
    if (nmults == 0 || nmults == MAX_OFFSET || offset != 0) {
        return 0;
    }

    i = 0;

    // copy/muls are stored like so:
    // [off][off][off][amt]
    for (size_t q = 0; q < nmults && i <= size - 2; ++q) {
        // Found a product
        if (mults[q] != 0 && offsets[q] != 0) {
            bf_log("%d += cell * %d\n", offsets[q], mults[q]);
            program[i].op = bf_opcode_copy_mul;
            program[i].amount = (mults[q] & 0xFF) | (offsets[q] << 8);
            ++i;
        }
    }

    // replace last op with a clear
    program[i++].op = bf_opcode_clear;

    bf_log("Copy/Multiply Optimization Complete\n");

    // Return how many opcodes we replaced
    return i;
}

// Don't bother switching. We just store and move on. This prevents a lot of calculations.
static void commit(int mode, int amount, bf_opcode **out)
{
    bf_log("commit: %c: %d\n", mode, amount);
    (*out)->op = mode;
    if (mode == bf_opcode_move || mode == bf_opcode_add)
        (*out)->amount = amount;
    (*out)++;
}

// Fills in the offset for a jump.
static bool fill_in_jump(bf_opcode *restrict start, bf_opcode **restrict opcodes_iterator, bool leaf)
{
    int32_t diff = start - (*opcodes_iterator);
    bf_log("diff: %d ",diff);
    (*opcodes_iterator)->amount = diff;
    diff = *opcodes_iterator - start;
    bf_log("%d\n", diff);
    start[0].amount = diff;

    if (leaf) {
        bf_log("leaf\n");
        bf_opcode *op = start;
        diff = update_copyloop(op, 1 + *opcodes_iterator - op);
        if (diff) {
            op = start + diff;
            while (op < *opcodes_iterator) {
                op->amount = bf_opcode_nop;
                op->op = bf_opcode_nop;
                ++op;
            }

            *opcodes_iterator = start + diff;
            return true;
        }
    }
    return false;
}


// Overwrites a clear loop with a zero loop
static void write_clear_loop(bf_opcode *restrict start, bf_opcode **restrict out)
{
    start->op = bf_opcode_clear;
    *out = start + 1;
}
#endif // BRAINFUCK_IR_H
