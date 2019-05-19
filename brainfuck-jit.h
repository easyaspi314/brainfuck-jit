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
#ifndef BRAINFUCK_JIT_H
#define BRAINFUCK_JIT_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * brainfuck()
 *
 * JIT compiles a Brainfuck string. The string is passed into code, with the corresponding length.
 */
void brainfuck(const char *code, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BRAINFUCK_JIT_H */
