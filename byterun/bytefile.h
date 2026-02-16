#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bytecode stream is byte-aligned; unaligned int loads via (int*) are UB and
// trip UBSan.
static inline unsigned char bytefile_read_u8(unsigned char **ip) {
  unsigned char v;
  memcpy(&v, *ip, sizeof(v));
  *ip += sizeof(v);
  return v;
}

static inline int bytefile_read_i32(unsigned char **ip) {
  int v;
  memcpy(&v, *ip, sizeof(v));
  *ip += sizeof(v);
  return v;
}

#define BYTE (bytefile_read_u8(&ip))
#define INT (bytefile_read_i32(&ip))

/* The unpacked representation of bytecode file */
typedef struct bytefile {
  char *string_ptr; /* A pointer to the beginning of the string table */
  int *public_ptr;  /* A pointer to the beginning of publics table    */
  char *code_ptr;   /* A pointer to the bytecode itself               */
  int *global_ptr;  /* A pointer to the global area                   */
  size_t code_size;
  int stringtab_size;   /* The size (in bytes) of the string table        */
  int global_area_size; /* The size (in words) of global area             */
  int public_symbols_number; /* The number of public symbols */
  char buffer[0];
} bytefile;

// Generic ops
static inline int get_public_count(const bytefile *const f) {
  return f->public_symbols_number;
}
static inline char *get_string(const bytefile *const f, int pos) {
  return &f->string_ptr[pos];
}
static inline char *get_public_name(const bytefile *const f, int i) {
  return get_string(f, f->public_ptr[i * 2]);
}
static inline int get_public_offset(const bytefile *const f, int i) {
  return f->public_ptr[i * 2 + 1];
}
static inline size_t get_code_size(const bytefile *const f) {
  return f->code_size;
}
static inline int32_t get_arg(const bytefile *const bf, int pos) {
  unsigned char *ip = (unsigned char *)bf->code_ptr + pos;
  (void)(BYTE); /* skip opcode byte */
  return (int32_t)INT;
}
static inline int32_t get_arg2(const bytefile *const bf, int pos) {
  unsigned char *ip = (unsigned char *)bf->code_ptr + pos;
  (void)(BYTE); /* skip opcode byte */
  (void)(INT);
  return (int32_t)INT;
}
/*
Prev layout:
| opcode (8 bits) | args_cnt (32 bits) | locals_cnt (32 bits)                         |
| opcode (8 bits) | args_cnt (32 bits) | stack_depth (16 bits) | locals_cnt (16 bits) |
*/
static inline void set_arg2_bighalf(const bytefile *const bf, int pos,
                                    uint16_t val) {
  unsigned char *ip = (unsigned char *)bf->code_ptr + pos;
  (void)(BYTE); /* skip opcode byte */
  (void)(INT);

  uint32_t raw;
  memcpy(&raw, ip, sizeof(raw));
  raw = (raw & 0x0000FFFFu) | ((uint32_t)val << 16);
  memcpy(ip, &raw, sizeof(raw));
}

static inline char *get_code_ptr(bytefile *f) { return f->code_ptr; }
static inline int get_global_area_size(const bytefile *const f) {
  return f->global_area_size;
}

// Success 0
// Error -1
int get_bytes(const struct bytefile *const bf, uint32_t pos, uint32_t len,
              const uint8_t **out_ptr);

struct bytefile *read_file(char *fname);
void destroy_file(struct bytefile *);
void dump_file(FILE *f, const struct bytefile *const);

#ifdef __cplusplus
}
#endif