#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "bytefile.h"

typedef enum bytecode {
  BINOP_HIGH = 0x00,
  LOW_ADD = 0x01,
  LOW_SUB = 0x02,
  LOW_MUL = 0x03,
  LOW_DIV = 0x04,
  LOW_MOD = 0x05,
  LOW_LT = 0x06,
  LOW_LE = 0x07,
  LOW_GT = 0x08,
  LOW_GE = 0x09,
  LOW_EQ = 0x0A,
  LOW_NE = 0x0B,
  LOW_AND = 0x0C,
  LOW_OR = 0x0D,
  CONST = 0x10,
  STRING = 0x11,
  SEXP = 0x12,
  STI = 0x13,
  STA = 0x14,
  JMP = 0x15,
  END = 0x16,
  RET = 0x17,
  DROP = 0x18,
  DUP = 0x19,
  SWAP = 0x1A,
  ELEM = 0x1B,
  LD_GLOBAL = 0x20,
  LD_LOCAL = 0x21,
  LD_ARGUMENT = 0x22,
  LD_CAPTURED = 0x23,
  LDA_GLOBAL = 0x30,
  LDA_LOCAL = 0x31,
  LDA_ARGUMENT = 0x32,
  LDA_CAPTURED = 0x33,
  ST_GLOBAL = 0x40,
  ST_LOCAL = 0x41,
  ST_ARGUMENT = 0x42,
  ST_CAPTURED = 0x43,
  CJMPZ = 0x50,
  CJMPNZ = 0x51,
  BEGIN = 0x52,
  CBEGIN = 0x53,
  CLOSURE = 0x54,
  CALLC = 0x55,
  CALL = 0x56,
  TAG = 0x57,
  ARRAY = 0x58,
  FAIL = 0x59,
  LINE = 0x5A,
  PATT_STR = 0x60,
  PATT_STRING = 0x61,
  PATT_ARRAY = 0x62,
  PATT_SEXP = 0x63,
  PATT_REF = 0x64,
  PATT_VAL = 0x65,
  PATT_FUN = 0x66,
  CALL_LREAD = 0x70,
  CALL_LWRITE = 0x71,
  CALL_LLENGTH = 0x72,
  CALL_LSTRING = 0x73,
  CALL_BARRAY = 0x74,
  STOP = 0xF0
} bytecode;

/* Static operand type for verification: what an instruction pushes on the stack
 */
typedef enum operand_type_e {
  OT_REF = 0,
  OT_IMM = 1,
  OT_UNKNOWN = 2,
} operand_type_e;

/* Returns the type of the value pushed by op (OT_UNKNOWN if op does not push
 * exactly one value) */
int get_pushed_operand_type(bytecode op);

int disassemble_instruction(FILE *f, const struct bytefile *const bf,
                            int offset, bytecode *const ret_opcode,
                            uint32_t *inc, uint32_t *dec);

#ifdef __cplusplus
}
#endif