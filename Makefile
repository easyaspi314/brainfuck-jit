CC := gcc
HEADERS = $(wildcard brainfuck-jit-*.h)
ifeq ($(DEBUG),)
CPPFLAGS := -DNDEBUG
CFLAGS := -O3 -Wall -Wextra -std=gnu99
else
CPPFLAGS := -DDEBUG
CFLAGS := -O0 -Wall -Wextra -std=gnu99 -g3
endif
brainfuck-jit: brainfuck-jit.o main.o
	$(CC) $(CFLAGS) $^ -o $@

brainfuck-interp: brainfuck-interp.o main.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c brainfuck-jit.h $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

brainfuck-interp.o: brainfuck-jit.c $(HEADERS) brainfuck-jit.h
	$(CC) $(CPPFLAGS) -DUSE_FALLBACK $(CFLAGS) -c $< -o $@

clean:
	-$(RM) -f brainfuck-jit brainfuck-jit.o brainfuck-interp.o brainfuck-interp main.o

.PHONY: clean
