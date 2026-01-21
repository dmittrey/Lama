#ifndef BYTECODE_H
#define BYTECODE_H

/* The unpacked representation of bytecode file */
typedef struct {
  char *string_ptr; /* A pointer to the beginning of the string table */
  int *public_ptr;  /* A pointer to the beginning of publics table    */
  char *code_ptr;   /* A pointer to the bytecode itself               */
  int *global_ptr;  /* A pointer to the global area                   */

  int stringtab_size;   /* The size (in bytes) of the string table        */
  int global_area_size; /* The size (in words) of global area             */
  int public_symbols_number; /* The number of public symbols */

  char buffer[];
} bytefile;

/* Gets a string from a string table by an index */
char *get_string(bytefile *f, int pos);

/* Gets a name for a public symbol */
char *get_public_name(bytefile *f, int i);

/* Gets an offset for a public symbol */
int get_public_offset(bytefile *f, int i);

/* Reads a binary bytecode file by name and unpacks it */
bytefile *parse_bc_file(char *fname);

#endif