#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INT (ip += sizeof(int), *(int *)(ip - sizeof(int)))
#define BYTE                                                                   \
  (ip += sizeof(unsigned char), *(unsigned char *)(ip - sizeof(unsigned char)))

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
  char *ip = bf->code_ptr + pos;
  (void)(BYTE); /* skip opcode byte */
  return (int32_t)INT;
}
static inline int32_t get_arg2(const bytefile *const bf, int pos) {
  char *ip = bf->code_ptr + pos;
  (void)(BYTE); /* skip opcode byte */
  (void)(INT);
  return (int32_t)INT;
}
static inline void set_arg2_bighalf(const bytefile *const bf, int pos,
                                    uint16_t val) {
  char *ip = bf->code_ptr + pos;
  (void)(BYTE); /* skip opcode byte */
  (void)(INT);
  memcpy(ip, &val, sizeof(uint16_t));
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