#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "brainfuck-jit.h"

int main(int argc, char *argv[])
{
    if (argc == 1) {
        const char test[] = ">++[<+++++++++++++>-]<[[>+>+<<-]>[<+>-]++++++++[>++++++++<-]>.[-]<<>++++++++++[>++++++++++[>++++++++++[>++++++++++[>++++++++++[>++++++++++[>++++++++++[-]<-]<-]<-]<-]<-]<-]<-]++++++++++.";
        brainfuck(test, sizeof(test));
    } else {
        // Easier to use unistd instead of stdio
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            printf("Could not open %s\n", argv[1]);
            return 1;
        }
        struct stat st;
        if (fstat(fd, &st) < 0) {
            printf("Error statting %s\n", argv[1]);
            close(fd);
            return 1;
        }
        off_t len = st.st_size;
        char *buf = (char *)malloc(len + 1);
        if (!buf) {
            puts("Out of memory");
            close(fd);
            return 1;
        }
        if (read(fd, buf, len) < len) {
            printf("Couldn't read %s\n", argv[1]);
            close(fd);
            free(buf);
            return 1;
        }
        close(fd);
        buf[len] = '\0';
        brainfuck(buf, len);
        free(buf);
    }
}

