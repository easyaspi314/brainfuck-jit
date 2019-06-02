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

#ifndef BRAINFUCK_BACKEND_C_H
#define BRAINFUCK_BACKEND_C_H

static const char init[] =
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <stdint.h>\n"
"\n"
"int main(void)\n"
"{\n"
"    uint8_t *cells = (uint8_t *)calloc(1, 65536);\n"
"    if (cells == NULL) {\n"
"        puts(\"Out of memory\");\n"
"        return 1;\n"
"    }\n"
"    uint8_t *cell = cells;\n";

static const char cleanup[] =
"    free(cells);\n"
"    return 0;\n"
"}\n";
static int indent = 4;
#define print(fmt, ...) printf("%*s" fmt, indent, "", ##__VA_ARGS__)

static void run_opcodes(bf_opcode *opcodes, size_t len)
{
    fwrite(init, 1, sizeof(init) - 1, stdout);
    int indent = 4;
    for (size_t i = 0; i < len; i++) {
        switch (opcodes[i].op) {
        case bf_opcode_add:
            print("*cell += %d;\n", opcodes[i].amount);
            break;
        case bf_opcode_move:
            print("cell += %d;\n", opcodes[i].amount);
            break;
        case bf_opcode_put:
            print("putchar(*cell);\n");
            break;
        case bf_opcode_get:
            print("*cell = getchar();\n");
            break;
        case bf_opcode_clear:
            print("*cell = 0;\n");
            break;
        case bf_opcode_start:
            print("while (*cell) {\n");
            indent += 4;
            break;
        case bf_opcode_end:
            indent -= 4;
            print("}\n");
            break;
        case bf_opcode_copy_mul:
            if ((opcodes[i].amount & 0xFF) == 1) {
                print("cell[%d] += *cell;\n", opcodes[i].amount >> 8);
            } else {
                print("cell[%d] += *cell * %d;\n", opcodes[i].amount >> 8, opcodes[i].amount & 0xFF);
            }
            break;
        default:
            break;
        }
    }
    fwrite(cleanup, 1, sizeof(cleanup) - 1, stdout);

}

#endif
