#include <stdio.h>
#include <stdlib.h>

#include "bytecode.h"

#define PUB_VAL_SIZE 2 * sizeof(uint32_t) // pos + offset
/*
| stringtab_size | global_area_size | public_symbols_number |
|                   publics table                           |
|                   string table                            |
|                   bytecode                                |
*/
static inline int header_size(const bytefile *const f) {
  return sizeof(f->stringtab_size) + sizeof(f->global_area_size) +
         sizeof(f->public_symbols_number);
}

/* Gets a string from a string table by an index */
char *get_string(const bytefile *const f, int pos) {
  if (pos < 0 || pos >= f->stringtab_size) {
    fprintf(stderr,
            "bytecode: invalid string table index: %d (table_size=%d)\n", pos,
            f->stringtab_size);
    return NULL;
  }
  return &f->string_ptr[pos];
}

/* Gets a name for a public symbol */
char *get_public_name(const bytefile *const f, int i) {
  if (i < 0 || i >= f->public_symbols_number) {
    fprintf(stderr, "bytecode: invalid public symbol index: %d (number=%d)\n",
            i, f->public_symbols_number);
    return NULL;
  }
  return get_string(f, f->public_ptr[i * 2]);
}

/* Gets an offset for a public symbol */
int get_public_offset(const bytefile *const f, int i) {
  if (i < 0 || i >= f->public_symbols_number) {
    fprintf(stderr, "bytecode: invalid public symbol index: %d (number=%d)\n",
            i, f->public_symbols_number);
    return 0;
  }
  return f->public_ptr[i * 2 + 1];
}

/* Reads a binary bytecode file by name and unpacks it */
bytefile *parse_bc_file(const char *const fname) {
  FILE *f = fopen(fname, "rb");
  if (!f) {
    fprintf(stderr, "Cannot open file: %s\n", fname);
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fprintf(stderr, "Failed to seek file\n");
    fclose(f);
    return NULL;
  }

  long end = ftell(f);
  if (end < 0) {
    fprintf(stderr, "Failed to tell file size\n");
    fclose(f);
    return NULL;
  }

  size_t size = (size_t)end;
  rewind(f);

  bytefile *file = (bytefile *)malloc(sizeof(bytefile) + size);
  if (!file) {
    fprintf(stderr, "Memory allocation failed\n");
    fclose(f);
    return NULL;
  }

  size_t rd = fread(&file->stringtab_size, 1, size, f);
  if (rd != size) {
    fprintf(stderr, "Failed to read file\n");
    free(file);
    fclose(f);
    return NULL;
  }
  fclose(f);

  if (size < header_size(file)) {
    fprintf(stderr, "Invalid bytecode: too small\n");
    free(file);
    return NULL;
  }
  if (file->stringtab_size < 0 || file->global_area_size < 0 ||
      file->public_symbols_number < 0) {
    fprintf(stderr, "Invalid bytecode: negative header field\n");
    free(file);
    return NULL;
  }

  size_t payload_bytes =
      size - header_size(file); /* bytes placed into buffer[] */
  size_t public_bytes = (size_t)file->public_symbols_number * PUB_VAL_SIZE;
  size_t string_bytes = (size_t)file->stringtab_size;

  if (public_bytes > payload_bytes ||
      public_bytes + string_bytes > payload_bytes) {
    fprintf(stderr, "Invalid bytecode: tables out of range\n");
    free(file);
    return NULL;
  }

  file->public_ptr = (int *)file->buffer;
  file->string_ptr = file->buffer + public_bytes;
  file->code_ptr = file->string_ptr + string_bytes;
  file->code_size = payload_bytes - public_bytes - string_bytes;

  file->global_ptr = (int *)calloc((size_t)file->global_area_size, sizeof(int));
  if (!file->global_ptr && file->global_area_size != 0) {
    fprintf(stderr, "Failed to allocate global area\n");
    free(file);
    return NULL;
  }

  return file;
}