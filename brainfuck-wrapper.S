
// A simple wrapper for the debug JIT output. Works on x86_64 Linux and macOS. It doesn't rely on libc.
//
// gcc -static -nostdlib brainfuck-wrapper.S bf.s
//

    // The better syntax
    .intel_syntax noprefix

    // Tunables
    .equ BUFFER_SIZE, 4096
    .equ NUM_CELLS, 65536

    .equ NEWLINE, 10
    .equ EOF, -1

    .equ STDIN, 0
    .equ STDOUT, 1

#ifdef __APPLE__
    // dunno why, but macOS needs 0x2000000 added to the syscall num
    .equ SYSCALL_SUM, 0x2000000
    .equ READ,   SYSCALL_SUM + 3 // SYS_read
    .equ WRITE,  SYSCALL_SUM + 4 // SYS_write
    .equ EXIT,   SYSCALL_SUM + 1 // SYS_exit
#else
    .equ READ,   0 // SYS_read
    .equ WRITE,  1 // SYS_write
    .equ EXIT,  60 // SYS_exit
#endif

// Automatically converts mov with immediate to xor eax, eax
.macro movi reg, val
    .if \val
        mov     \reg, \val
    .else
        xor     \reg, \reg
    .endif
.endm

// Our globals.
#ifdef __APPLE__
    .section __DATA,__bss
#else
    .bss
#endif
// static char line_buffer[BUFFER_SIZE];
line_buffer:
    .space BUFFER_SIZE
// static unsigned char cells[NUM_CELLS] = {0};
cells:
    .zero NUM_CELLS
// static int line_buffer_pos = 0
line_buffer_pos:
    .long 0
// static bool needs_flush = false;
needs_flush:
    .byte 0

#ifdef __APPLE__
    .section __TEXT,__text
#else
    .text
#endif

// void _start(void)
// {
//     fuck(cells, &my_putchar, &my_getchar);
//     while (needs_flush) {
//         my_getchar();
//     }
//     exit(0);
// }

#ifdef __APPLE__
.globl start
start:
#else
.globl _start
_start:
#endif
    push    r13
    push    r15
    lea     r13, [rip + line_buffer]
    xor     r15d, r15d
    lea     rdi, [rip + cells]
    lea     rsi, [rip + my_putchar]
    lea     rdx, [rip + my_getchar]
    call    fuck
    pop     r15
    pop     r13
.Lgetchar_loop:
    cmp     byte ptr[rip + needs_flush], 0
    je      .Lexit

    call    my_getchar
    jmp     .Lgetchar_loop
.Lexit:
    movi    rax, EXIT
    movi    edi, 0
    syscall
    // UNREACHABLE

// syscall implementation of putchar.
// int my_putchar(int c)
// {
//     int ret = 0;
//     line_buffer[line_buffer_pos++] = c;
//     if (c == '\n' || line_buffer_pos == BUFFER_SIZE) {
//         ret = write(STDOUT_FILENO, line_buffer, line_buffer_pos);
//         line_buffer_pos = 0;
//     }
//     return ret;
// }

my_putchar:
    xor     eax, eax
    mov     byte ptr[r13 + r15], dil
    inc     r15d
    cmp     dil, NEWLINE
    je      .Loutput
    cmp     r15d, BUFFER_SIZE
    je      .Loutput
    ret
.Loutput:
    movi    eax, WRITE
    movi    edi, STDOUT
    mov     rsi, r13
    mov     edx, r15d
    syscall
    xor     r15d, r15d
    ret

// syscall implementation of getchar. It discards upper bits though.
// char my_getchar(void)
// {
//     if (line_buffer_pos != 0) {
//         fflush(stdout);
//     }
//     int ret = EOF;
//     read(STDIN_FILENO, &ret, 1);
//     needs_flush = (ret != EOF && ret != '\n');
//     return (char)ret;
// }

my_getchar:
    test    r15d, r15d
    jz      .Lno_call_output
    call    .Loutput
 .Lno_call_output:
    // give us stack space and a default value
    push    EOF
    movi    eax, READ
    movi    edi, STDIN
    mov     rsi, rsp
    movi    edx, 1
    syscall

    // get out last value
    pop     rax
    cmp     eax, EOF
    setne   byte ptr[rip + needs_flush]
    je      .Lgetchar_ret
    cmp     al, NEWLINE
    setne   byte ptr[rip + needs_flush]
.Lgetchar_ret:
    ret
