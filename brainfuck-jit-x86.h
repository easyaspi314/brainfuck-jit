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

// JIT backend for i386 and x86_64
#ifndef BRAINFUCK_JIT_X86_H
#define BRAINFUCK_JIT_X86_H

#if defined(__i386__) || defined(_M_IX86)
#    define JIT_I386
#elif !(defined(__x86_64__) || defined(_M_X64) || defined(__amd64__) || defined(_M_AMD64))
#    error "This JIT code is designed exclusively for x86 and x86_64."
#endif

#ifndef BRAINFUCK_JIT_C
#  error "This file is only to be included from brainfuck-jit.c"
#endif
#include <string.h>

#ifdef JIT_I386
#   define RBX "ebx"
#else
#   define RBX "rbx"
#endif

// cmp + jne = 9 bytes
#define MAX_INSN_LEN 16
// size of init[]
#define INIT_LEN 14
// size of cleanup[]
#define CLEANUP_LEN 6
typedef uint8_t raw_opcode;

// Writes the initialization code for our JIT.
static void write_init_code(uint8_t *restrict out, size_t *restrict pos)
{

    // Initialization routine
#ifdef JIT_I386

    // cdecl calling conventions state that our parameters will be at esp+4.
    // We don't have many registers on i386, so we leave the putchar
    // and getchar pointers on the stack.
    const uint8_t init[] = {
        // Set up our parameterz.
        // push ebp
        0x55,
        // push ebx
        0x53,
        // mov ebx, dword ptr[esp + 12]
        0x8b, 0x5c, 0x24, 0x0c,
    };
    // putchar: esp + 16
    // getchar: esp + 20
    bf_log(
        "fuck:\n"
        "        push    ebp\n"
        "        push    ebx\n"
        "        mov     ebx, dword ptr[esp + 12]\n"
    );

#else // x86_64

    // System V calling conventions dictate that the first three parameters are in rdi, rsi, and rdx.
    // Windows calling conventions say that the first three parameters are rcx, rdx, and r8.
    //
    // Additionally, they both say that rbx, rbp, and r12-r15 are callee saved: In order to use them in your function,
    // you need to push and pop them. We use these registers to store data, as we don't need to worry about pushing and
    // popping in our own code.
    const uint8_t init[] = {
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

    bf_log(
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
#endif // !JIT_I386
    memcpy(out + *pos, init, sizeof(init));
    *pos += sizeof(init);
}

static inline uint8_t log_2(int32_t val) {
     switch (val) {
          case 2:   return 1;
          case -2:  return 1;
          case 4:   return 2;
          case -4:  return 2;
          case 8:   return 3;
          case -8:  return 3;
          case 16:  return 4;
          case -16: return 4;
          case 32:  return 5;
          case -32: return 5;
          case 64:  return 6;
          case -64: return 6;
          default:  return 0;
    }
}

static inline void do_multiply(int32_t op, uint8_t *restrict out, size_t *restrict pos)
{
    int32_t offset = op >> 8;
    int32_t amount = (int8_t)op; // sign extend

    // Store the current cell in al
    bf_log("        mov     al, byte ptr[" RBX "]\n");
    out[(*pos)++] = 0x8a;
    out[(*pos)++] = 0x03;

    switch (amount) {
    // 1 and 2 are special cases.
    case 1:
    case -1:
        // don't modify al
        break;
    case -2:
    case 2:
        // shorter to add to itself
        bf_log("        add      al, al\n");
        out[(*pos)++] = 0x00;
        out[(*pos)++] = 0xc0;
        break;
    default: {
        // Either do a shift left with log2(amount), or do an imul.
        uint8_t shift = log_2(amount);
        if (shift) {
            // Shift left
            bf_log("        shl    al, %d\n", shift);
            out[(*pos)++] = 0xc0;
            out[(*pos)++] = 0xe0;
            out[(*pos)++] = shift;
        } else {
            // Get our multiple
            bf_log("        mov    cl, %u\n", amount);
            out[(*pos)++] = 0xb1;
            out[(*pos)++] = abs(amount);
            // AX = AL * CL, discard upper bits
            bf_log("        imul   cl\n");
            out[(*pos)++] = 0xf6;
            out[(*pos)++] = 0xe9;
        }
        }
        break;
    }
    // multiply by negative - DRY: only the first byte differs between add and sub
    if (amount < 0) {
        bf_log("        sub     byte ptr[" RBX "%+d], al\n", offset);
        out[(*pos)++] = 0x28;
    } else {
        bf_log("        add     byte ptr[" RBX "%+d], al\n", offset);
        out[(*pos)++] = 0x00;
    }
    out[(*pos)++] = 0x83;
    // add offset
    memcpy(out + *pos, &offset, sizeof(int32_t));
    *pos += sizeof(int32_t);
}

/// Compiles a single opcode.
static void compile_opcode(bf_opcode *restrict opcode, uint8_t *restrict out, size_t *restrict pos)
{
    if (opcode->op == bf_opcode_nop)
        return;
    switch (opcode->op) {
    case bf_opcode_move:
        if (opcode->amount == 0)
            return;
        if (opcode->amount == 1) {
            bf_log("        inc     " RBX "\n");
#ifndef JIT_I386
            // skip REX.W byte on x86
            out[(*pos)++] = 0x48;
#endif
            out[(*pos)++] = 0xff;
            out[(*pos)++] = 0xc3;
        } else if (opcode->amount == -1) {
            bf_log("        dec     " RBX "\n");
#ifndef JIT_I386
            out[(*pos)++] = 0x48;
#endif
            out[(*pos)++] = 0xff;
            out[(*pos)++] = 0xcb;
        } else {
            // overflow with sign extension on negative
            bf_log("        add     " RBX ", %i\n", opcode->amount);
#ifndef JIT_I386
            out[(*pos)++] = 0x48;
#endif
            out[(*pos)++] = 0x81;
            out[(*pos)++] = 0xc3;
            memcpy(out + *pos, &opcode->amount, sizeof(int32_t));
            *pos += sizeof(int32_t);
        }
        return;
    case bf_opcode_add: // Add / subtract
        if (opcode->amount == 0)
            return;

        if (opcode->amount == 1) {
            bf_log("        inc     byte ptr[" RBX "]\n");
            out[(*pos)++] = 0xfe;
            out[(*pos)++] = 0x03;
        } else if (opcode->amount == -1) {
            bf_log("        dec     byte ptr[" RBX "]\n");
            out[(*pos)++] = 0xfe;
            out[(*pos)++] = 0x0b;
        } else {
            // overflow with the sign extension on negative
            bf_log("        add     byte ptr[" RBX "], %i\n", opcode->amount & 0xFF);
            out[(*pos)++] = 0x80;
            out[(*pos)++] = 0x03;
            out[(*pos)++] = opcode->amount & 0xFF;
        }
        return;
    case bf_opcode_start: // Opening brace
        bf_log("        cmp     byte ptr[" RBX "], 0\n");
        out[(*pos)++] = 0x80;
        out[(*pos)++] = 0x3b;
        out[(*pos)++] = 0x00;

        bf_log("        je      <tbd>\n");
        out[(*pos)++] = 0x0f;
        out[(*pos)++] = 0x84;

        // Dynamic programming: store the offset for bf_opcode_end
        opcode->amount = (int32_t)(*pos);

        *pos += 4; // placeholder, we insert the address later.
        return;
    case bf_opcode_end: { // Closing brace
        bf_log("        cmp     byte ptr[" RBX "], 0\n");
        out[(*pos)++] = 0x80;
        out[(*pos)++] = 0x3b;
        out[(*pos)++] = 0x00;

        bf_log("        jne     <tbd>\n");
        out[(*pos)++] = 0x0f;
        out[(*pos)++] = 0x85;

        // Fill in the je and jne. The offset of our start is in start.
        bf_opcode *start = &opcode[opcode->amount];

        int32_t offset_to = start->amount - *pos;
        int32_t offset_from = *pos - start->amount;
        memcpy(out + start->amount, &offset_from, sizeof(int32_t));
        memcpy(out + *pos, &offset_to, sizeof(int32_t));
        *pos += 4;

        return;
    }
    case bf_opcode_put:
#ifdef JIT_I386
        // cdecl is beautiful
        // Set up params
        bf_log("        movzx   eax, byte ptr[ebx]\n");
        out[(*pos)++] = 0x0f;
        out[(*pos)++] = 0xb6;
        out[(*pos)++] = 0x03;
        bf_log("        push    eax\n");
        out[(*pos)++] = 0x50;
        // Call putchar. It is at esp + 16 + 4 (from the push eax)
        bf_log("        call    dword ptr[esp + 20]\n");
        out[(*pos)++] = 0xff;
        out[(*pos)++] = 0x54;
        out[(*pos)++] = 0x24;
        out[(*pos)++] = 0x14;
        // Clean up the stack
        bf_log("        add     esp, 4\n");
        out[(*pos)++] = 0x83;
        out[(*pos)++] = 0xc4;
        out[(*pos)++] = 0x04;
#else // x86_64
#ifdef _WIN32 // Windows ABI
        bf_log("        movzx   ecx, byte ptr[rbx]\n"); // zero extend to int
        out[(*pos)++] = 0x0f;
        out[(*pos)++] = 0xb6;
        out[(*pos)++] = 0x0b;
#else // System V ABI
        bf_log("        movzx   edi, byte ptr[rbx]\n"); // zero extend to int
        out[(*pos)++] = 0x0f;
        out[(*pos)++] = 0xb6;
        out[(*pos)++] = 0x3b;
#endif

        bf_log("        call    r14\n"); // call putchar (which is in r14)
        out[(*pos)++] = 0x41;
        out[(*pos)++] = 0xff;
        out[(*pos)++] = 0xd6;
#endif // x86_64
        return;
    case bf_opcode_get: // Calls getchar (r12/stack), and then stores the result in the address of rbx.
#ifdef JIT_I386
        bf_log("        call    dword ptr[esp + 20]\n");
        out[(*pos)++] = 0xff;
        out[(*pos)++] = 0x54;
        out[(*pos)++] = 0x24;
        out[(*pos)++] = 0x14;
#else
        bf_log("        call    r12\n");
        out[(*pos)++] = 0x41;
        out[(*pos)++] = 0xff;
        out[(*pos)++] = 0xd4;
#endif
        bf_log("        mov     byte ptr[" RBX "], al\n");
        out[(*pos)++] = 0x88;
        out[(*pos)++] = 0x03;
        return;

    case bf_opcode_copy_mul: {
        // If we are multiplying by zero we ignore it.
        if ((opcode->amount & 0xFF) == 0 || (opcode->amount >> 8) == 0) // nop
            break;
        do_multiply(opcode->amount, out, pos);
        return;
    }
    case bf_opcode_clear:
        bf_log("        mov     byte ptr[" RBX "], 0\n");
        out[(*pos)++] = 0xc6;
        out[(*pos)++] = 0x03;
        out[(*pos)++] = 0x00;
        return;
    default:
        return;
    }
}

// Writes the cleanup code for our JIT.
static void write_cleanup_code(uint8_t *restrict out, size_t *restrict pos)
{
#ifdef JIT_I386
    // Clean up code: Restores the stack, pops registers, and returns.
    const uint8_t cleanup[] = {
        // pop ebx
        0x5b,
        // pop ebp
        0x5d,
        // ret
        0xc3,
    };
    bf_log(
        "        pop     ebx\n"
        "        pop     ebp\n"
        "        ret\n"
    );
#else
    // Clean up code: Restores the stack, pops registers, and returns.
    const uint8_t cleanup[] = {
        // pop r14
        0x41, 0x5e,
        // pop r12
        0x41, 0x5c,
        // pop rbx
        0x5b,
        // ret
        0xc3,
    };

    bf_log(
        "        pop     r14\n"
        "        pop     r12\n"
        "        pop     rbx\n"
        "        ret\n"
    );
#endif
    memcpy(out + *pos, cleanup, sizeof(cleanup));
    *pos += sizeof(cleanup);

}

#endif // BRAINFUCK_JIT_X86_H
