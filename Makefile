CC := gcc
ifeq ($(DEBUG),)
CPPFLAGS := -DNDEBUG
CFLAGS := -O3 -Wall -Wextra -std=gnu99
else
CPPFLAGS := -DDEBUG
CFLAGS := -O0 -Wall -Wextra -std=gnu99 -g3
endif
brainfuck-jit: brainfuck-jit.o main.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c brainfuck-jit.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	-$(RM) -f brainfuck-jit brainfuck-jit.o main.o

.PHONY: clean
