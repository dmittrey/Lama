#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "bytefile.h"

int disassemble_instruction(FILE *f, bytefile *bf, int offset,
                            unsigned char *ret_opcode);

#ifdef __cplusplus
}
#endif