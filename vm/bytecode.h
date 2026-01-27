#ifndef BYTECODE_H
#define BYTECODE_H

#include <stddef.h>

/* The unpacked representation of bytecode file */
typedef struct {
  char *string_ptr; /* A pointer to the beginning of the string table */
  int *public_ptr;  /* A pointer to the beginning of publics table    */
  char *code_ptr;   /* A pointer to the bytecode itself               */
  size_t code_size; /* The size (in bytes) of bytecode area           */
  int *global_ptr;  /* A pointer to the global area                   */

  int stringtab_size;        /* The size (in bytes) of the string table */
  int global_area_size;      /* The size (in words) of global area */
  int public_symbols_number; /* The number of public symbols */

  char buffer[];
} bytefile;

/* Gets a string from a string table by an index */
char *get_string(const bytefile *const f, int pos);

/* Gets a name for a public symbol */
char *get_public_name(const bytefile *const f, int i);

/* Gets an offset for a public symbol */
int get_public_offset(const bytefile *const f, int i);

/* Reads a binary bytecode file by name and unpacks it */
bytefile *parse_bc_file(const char *const fname);

#endif