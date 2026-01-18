#include <stdio.h>
#include <stdlib.h>

#include "bytecode.h"

/* Gets a string from a string table by an index */
char *get_string(bytefile *f, int pos)
{
  return &f->string_ptr[pos];
}

/* Gets a name for a public symbol */
char *get_public_name(bytefile *f, int i)
{
  return get_string(f, f->public_ptr[i * 2]);
}

/* Gets an offset for a public symbol */
int get_public_offset(bytefile *f, int i)
{
  return f->public_ptr[i * 2 + 1];
}

/* Reads a binary bytecode file by name and unpacks it */
bytefile *parse_bc_file(char *fname)
{
  FILE *f = fopen(fname, "rb");
  long size;
  bytefile *file;

  if (f == 0)
  {
    fprintf(stderr, "Cannot open file: %s\n", fname);
    return NULL;
  }

  if (fseek(f, 0, SEEK_END) == -1)
  {
    fprintf(stderr, "Failed to seek file\n");
    fclose(f);
    return NULL;
  }

  file = (bytefile *)malloc(sizeof(int) * 4 + (size = ftell(f)));

  if (file == 0)
  {
    fprintf(stderr, "Memory allocation failed\n");
    fclose(f);
    return NULL;
  }

  rewind(f);

  if (size != fread(&file->stringtab_size, 1, size, f))
  {
    fprintf(stderr, "Failed to read file\n");
    free(file);
    fclose(f);
    return NULL;
  }

  fclose(f);

  file->string_ptr = &file->buffer[file->public_symbols_number * 2 * sizeof(int)];
  file->public_ptr = (int *)file->buffer;
  file->code_ptr = &file->string_ptr[file->stringtab_size];
  file->global_ptr = (int *)malloc(file->global_area_size * sizeof(int));

  return file;
}