#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "bytefile.h"

typedef enum bytecode {
  OP_BINOP_HIGH = 0x00,
  OP_LOW_ADD = 0x01,
  OP_LOW_SUB = 0x02,
  OP_LOW_MUL = 0x03,
  OP_LOW_DIV = 0x04,
  OP_LOW_MOD = 0x05,
  OP_LOW_LT = 0x06,
  OP_LOW_LE = 0x07,
  OP_LOW_GT = 0x08,
  OP_LOW_GE = 0x09,
  OP_LOW_EQ = 0x0A,
  OP_LOW_NE = 0x0B,
  OP_LOW_AND = 0x0C,
  OP_LOW_OR = 0x0D,
  OP_CONST = 0x10,
  OP_STRING = 0x11,
  OP_SEXP = 0x12,
  OP_STI = 0x13,
  OP_STA = 0x14,
  OP_JMP = 0x15,
  OP_END = 0x16,
  OP_RET = 0x17,
  OP_DROP = 0x18,
  OP_DUP = 0x19,
  OP_SWAP = 0x1A,
  OP_ELEM = 0x1B,
  OP_LD_GLOBAL = 0x20,
  OP_LD_LOCAL = 0x21,
  OP_LD_ARGUMENT = 0x22,
  OP_LD_CAPTURED = 0x23,
  OP_LDA_GLOBAL = 0x30,
  OP_LDA_LOCAL = 0x31,
  OP_LDA_ARGUMENT = 0x32,
  OP_LDA_CAPTURED = 0x33,
  OP_ST_GLOBAL = 0x40,
  OP_ST_LOCAL = 0x41,
  OP_ST_ARGUMENT = 0x42,
  OP_ST_CAPTURED = 0x43,
  OP_CJMPZ = 0x50,
  OP_CJMPNZ = 0x51,
  OP_BEGIN = 0x52,
  OP_CBEGIN = 0x53,
  OP_CLOSURE = 0x54,
  OP_CALLC = 0x55,
  OP_CALL = 0x56,
  OP_TAG = 0x57,
  OP_ARRAY = 0x58,
  OP_FAIL = 0x59,
  OP_LINE = 0x5A,
  OP_PATT_STR = 0x60,
  OP_PATT_STRING = 0x61,
  OP_PATT_ARRAY = 0x62,
  OP_PATT_SEXP = 0x63,
  OP_PATT_REF = 0x64,
  OP_PATT_VAL = 0x65,
  OP_PATT_FUN = 0x66,
  OP_CALL_LREAD = 0x70,
  OP_CALL_LWRITE = 0x71,
  OP_CALL_LLENGTH = 0x72,
  OP_CALL_LSTRING = 0x73,
  OP_CALL_BARRAY = 0x74,
  OP_STOP = 0xFF
} bytecode;

typedef uint32_t stkdepth; // Stack depth

int disassemble_instruction(FILE *f, const struct bytefile *const bf,
                            int offset, bytecode *const ret_opcode,
                            stkdepth *inc, stkdepth *dec);

#ifdef __cplusplus
}
#endif