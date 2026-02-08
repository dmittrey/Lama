#pragma once

#include <stddef.h>
#include <stdint.h>

// Generic bytefile
struct bytefile;

#ifdef __cplusplus
extern "C" {
#endif

// Generic ops
int get_public_count(const struct bytefile *const);
char *get_public_name(const struct bytefile *const, int pos);
int get_public_offset(const struct bytefile *const, int pos);
size_t get_code_size(const struct bytefile *const);
int32_t get_arg(const struct bytefile *const, int pos);

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