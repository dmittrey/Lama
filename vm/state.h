#ifndef STATE_H
#define STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../runtime/runtime.h"

#include "bytecode.h"
#include "callstack.h"
#include "error.h"

extern char *__ip;
extern bytefile *__bf;

char *ip_base() { return __bf->code_ptr; }
uint32_t ip_offset() { return __ip - __bf->code_ptr; }
error_code_e ip_jmp(int32_t offset) {
  if (offset < 0) {
    fprintf(stderr, "vm: jump offset negative: %d at ip=0x%.8lx\n", offset,
            __ip - __bf->code_ptr);
    return ERROR_JUMP_OFFSET_NEGATIVE;
  }
  if (offset >= __bf->code_size) {
    fprintf(stderr,
            "vm: jump offset out of range: %d(code size=%lu) at ip=0x%.8lx\n",
            offset, __bf->code_size, __ip - __bf->code_ptr);
    return ERROR_JUMP_OFFSET_OUT_OF_RANGE;
  }

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
char *bc_read_string() { return get_string(__bf, bc_read_int()); }

#endif
