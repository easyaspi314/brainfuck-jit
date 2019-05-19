CC := gcc
CFLAGS := -O3 -DNDEBUG

brainfuck-jit: brainfuck-jit.o main.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c brainfuck-jit.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	-$(RM) -f brainfuck-jit brainfuck-jit.o main.o

.PHONY: clean
