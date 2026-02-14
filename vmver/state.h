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
static inline void bc_require_bytes(size_t n, const char *what) {
  char *end = ip_end();
  if (__ip >= end) {
    failure("bytecode: ip at end while reading %s (ip_off=%lu, size=%lu)\n",
            what, (unsigned long)ip_offset(), (unsigned long)__bf->code_size);
  }
  if ((size_t)(end - __ip) < n) {
    failure("bytecode: read past end for %s (ip_off=%lu, need=%lu, size=%lu)\n",
            what, (unsigned long)ip_offset(), (unsigned long)n,
            (unsigned long)__bf->code_size);
  }
}
error_code_e ip_jmp(int32_t offset) {
  __ip = ip_base() + offset;
  return ERROR_NONE;
}

/* Bytecode */
int32_t bc_read_int() {
  int32_t value = 0;
  bc_require_bytes(sizeof(int32_t), "int32");
  memcpy(&value, __ip, sizeof(int32_t));
  __ip += sizeof(int32_t);
  return value;
}
char bc_read_byte() {
  char value = 0;
  bc_require_bytes(sizeof(char), "byte");
  memcpy(&value, __ip, sizeof(char));
  __ip += sizeof(char);
  return value;
}
const char *bc_read_string() {
  int32_t idx = bc_read_int();
  const char *s = get_string(__bf, idx);
  if (!s) {
    failure("bytecode: null string at index %d (ip_off=%lu)\n", idx,
            (unsigned long)ip_offset());
  }
  return s;
}

#endif
