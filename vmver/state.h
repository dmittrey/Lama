#ifndef STATE_H
#define STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../runtime/runtime.h"

#include "bytefile.h"
#include "callstack.h"
#include "error.h"

extern char *__ip;
extern bytefile *__bf;

char *ip_base() { return __bf->code_ptr; }
uint32_t ip_offset() { return __ip - __bf->code_ptr; }
static inline char *ip_end() { return __bf->code_ptr + __bf->code_size; }
error_code_e ip_jmp(int32_t offset) {
  __ip = ip_base() + offset;
  return ERROR_NONE;
}

/* Bytecode */
int32_t bc_read_int() {
  int32_t value = 0;
  memcpy(&value, __ip, sizeof(int32_t));
  __ip += sizeof(int32_t);
  return value;
}
char bc_read_byte() {
  char value = 0;
  memcpy(&value, __ip, sizeof(char));
  __ip += sizeof(char);
  return value;
}
const char *bc_read_string() {
  int32_t idx = bc_read_int();
  const char *s = get_string(__bf, idx);
  return s;
}

#endif
