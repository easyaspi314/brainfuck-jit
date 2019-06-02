
// Allocates a buffer using mmap, copies opcodes, marks executable, and runs.
static void run_opcodes(bf_opcode *restrict ir, size_t len)
{
    size_t i = 0, pos = 0;
    size_t memlen = len * MAX_INSN_LEN + INIT_LEN + CLEANUP_LEN;
    raw_opcode *opcodes = alloc_opcodes(memlen);
    if (opcodes == NULL) {
        return;
    }

    write_init_code(opcodes, &pos);
    while (i < len) {
         compile_opcode(&ir[i], opcodes, &pos);
         ++i;
    }
    write_cleanup_code(opcodes, &pos);
#ifdef DEBUG
    FILE *f = fopen("bf.s", "w");
    // asm file with .byte directives
    fprintf(f,  "        .text\n"
                "        .globl fuck\n"
                "fuck:\n");
    for (i = 0; i < pos; i++) {
        fprintf(f, "        .byte %#02x\n", opcodes[i]);
    }
    fclose(f);
#endif
    // Mark our region as R^X
    protect_opcodes(opcodes, memlen);

    // Cast to a function pointer
    brainfuck_t fuck = (brainfuck_t)opcodes;
    // and fuck it!
    uint8_t *cells = (uint8_t *)calloc(1, 65535), *cell = cells;

    fuck(cell, &putchar, &getchar);

    free(cells);

    // cleanup
    dealloc_opcodes(opcodes, memlen);

}
