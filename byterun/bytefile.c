/* Lama SM Bytecode interpreter */

#include "../runtime/runtime.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytefile.h"
#include "disasm.h"

void *__start_custom_data;
void *__stop_custom_data;

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

#define PUB_VAL_SIZE 2 * sizeof(uint32_t) // pos + offset

#define INT (ip += sizeof(int), *(int *)(ip - sizeof(int)))
#define BYTE *ip++
#define STRING get_string(bf, INT)
#define FAIL failure("ERROR: invalid opcode %d-%d\n", h, l)

/* Gets a string from a string table by an index */
char *get_string(const bytefile *const f, int pos) {
  return &f->string_ptr[pos];
}

/* Implement bytefile.h */
int get_public_count(const bytefile *const f) {
  return f->public_symbols_number;
}

/* Gets a name for a public symbol */
char *get_public_name(const bytefile *const f, int i) {
  return get_string(f, f->public_ptr[i * 2]);
}

/* Gets an offset for a publie symbol */
int get_public_offset(const bytefile *const f, int i) {
  return f->public_ptr[i * 2 + 1];
}

size_t get_code_size(const bytefile *const f) { return f->code_size; }

int32_t get_arg(const bytefile *const bf, int pos) {
  char *ip = bf->code_ptr + pos;
  (void)(BYTE); /* skip opcode byte */
  return (int32_t)INT;
}

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

/* Reads a binary bytecode file by name and unpacks it */
bytefile *read_file(char *fname) {
  FILE *f = fopen(fname, "rb");
  long size;
  bytefile *file;

  if (f == 0) {
    failure("%s\n", strerror(errno));
  }

  if (fseek(f, 0, SEEK_END) == -1) {
    failure("%s\n", strerror(errno));
  }

  file = (bytefile *)malloc(offsetof(bytefile, stringtab_size) +
                            (size = ftell(f)));

  if (file == 0) {
    failure("*** FAILURE: unable to allocate memory.\n");
  }

  rewind(f);

  if (size != fread(&file->stringtab_size, 1, size, f)) {
    failure("%s\n", strerror(errno));
  }

  fclose(f);

  file->string_ptr =
      &file->buffer[file->public_symbols_number * 2 * sizeof(int)];
  file->public_ptr = (int *)file->buffer;
  file->code_ptr = &file->string_ptr[file->stringtab_size];
  file->global_ptr = (int *)malloc(file->global_area_size * sizeof(int));

  size_t payload_bytes =
      size - header_size(file); /* bytes placed into buffer[] */
  size_t public_bytes = (size_t)file->public_symbols_number * PUB_VAL_SIZE;
  size_t string_bytes = (size_t)file->stringtab_size;
  file->code_size = payload_bytes - public_bytes - string_bytes;

  return file;
}

void destroy_file(bytefile *bf) {
  free(bf->global_ptr);
  free(bf);
}

int get_bytes(const bytefile *const bf, uint32_t pos, uint32_t len,
              const uint8_t **out_ptr) {
  if (!bf || !out_ptr)
    return -1;

  // защита от переполнения
  size_t code_size = bf->code_size;
  if ((size_t)pos > code_size)
    return -1;
  if ((size_t)len > code_size - (size_t)pos)
    return -1;

  *out_ptr = (const uint8_t *)bf->code_ptr + pos;
  return 0;
}

/* Disassembles the bytecode instruction */
int disassemble_instruction(FILE *f, const bytefile *const bf, int pos,
                            bytecode *ret_opcode) {
  char *ip = bf->code_ptr + pos;
  char *ops[] = {
      "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "!!"};
  char *pats[] = {"=str", "#string", "#array", "#sexp", "#ref", "#val", "#fun"};
  char *lds[] = {"LD", "LDA", "ST"};

  char x = BYTE, h = (x & 0xF0) >> 4, l = x & 0x0F;

  if (ret_opcode)
    *ret_opcode = x;

  fprintf(f, "0x%.8lx:\t", ip - bf->code_ptr + pos - 1);

  switch (h) {
  case 15:
    goto stop;

  /* BINOP */
  case 0:
    fprintf(f, "BINOP\t%s", ops[l - 1]);
    break;

  case 1:
    switch (l) {
    case 0:
      fprintf(f, "CONST\t%d", INT);
      break;

    case 1:
      fprintf(f, "STRING\t%s", STRING);
      break;

    case 2:
      fprintf(f, "SEXP\t%s ", STRING);
      fprintf(f, "%d", INT);
      break;

    case 3:
      fprintf(f, "STI");
      break;

    case 4:
      fprintf(f, "STA");
      break;

    case 5:
      fprintf(f, "JMP\t0x%.8x", INT);
      break;

    case 6:
      fprintf(f, "END");
      break;

    case 7:
      fprintf(f, "RET");
      break;

    case 8:
      fprintf(f, "DROP");
      break;

    case 9:
      fprintf(f, "DUP");
      break;

    case 10:
      fprintf(f, "SWAP");
      break;

    case 11:
      fprintf(f, "ELEM");
      break;

    default:
      FAIL;
    }
    break;

  case 2:
  case 3:
  case 4:
    fprintf(f, "%s\t", lds[h - 2]);
    switch (l) {
    case 0:
      fprintf(f, "G(%d)", INT);
      break;
    case 1:
      fprintf(f, "L(%d)", INT);
      break;
    case 2:
      fprintf(f, "A(%d)", INT);
      break;
    case 3:
      fprintf(f, "C(%d)", INT);
      break;
    default:
      FAIL;
    }
    break;

  case 5:
    switch (l) {
    case 0:
      fprintf(f, "CJMPz\t0x%.8x", INT);
      break;

    case 1:
      fprintf(f, "CJMPnz\t0x%.8x", INT);
      break;

    case 2:
      fprintf(f, "BEGIN\t%d ", INT);
      fprintf(f, "%d", INT);
      break;

    case 3:
      fprintf(f, "CBEGIN\t%d ", INT);
      fprintf(f, "%d", INT);
      break;

    case 4:
      fprintf(f, "CLOSURE\t0x%.8x", INT);
      {
        int n = INT;
        for (int i = 0; i < n; i++) {
          switch (BYTE) {
          case 0:
            fprintf(f, "G(%d)", INT);
            break;
          case 1:
            fprintf(f, "L(%d)", INT);
            break;
          case 2:
            fprintf(f, "A(%d)", INT);
            break;
          case 3:
            fprintf(f, "C(%d)", INT);
            break;
          default:
            FAIL;
          }
        }
      };
      break;

    case 5:
      fprintf(f, "CALLC\t%d", INT);
      break;

    case 6:
      fprintf(f, "CALL\t0x%.8x ", INT);
      fprintf(f, "%d", INT);
      break;

    case 7:
      fprintf(f, "TAG\t%s ", STRING);
      fprintf(f, "%d", INT);
      break;

    case 8:
      fprintf(f, "ARRAY\t%d", INT);
      break;

    case 9:
      fprintf(f, "FAIL\t%d", INT);
      fprintf(f, "%d", INT);
      break;

    case 10:
      fprintf(f, "LINE\t%d", INT);
      break;

    default:
      FAIL;
    }
    break;

  case 6:
    fprintf(f, "PATT\t%s", pats[l]);
    break;

  case 7: {
    switch (l) {
    case 0:
      fprintf(f, "CALL\tLread");
      break;

    case 1:
      fprintf(f, "CALL\tLwrite");
      break;

    case 2:
      fprintf(f, "CALL\tLlength");
      break;

    case 3:
      fprintf(f, "CALL\tLstring");
      break;

    case 4:
      fprintf(f, "CALL\tBarray\t%d", INT);
      break;

    default:
      FAIL;
    }
  } break;

  default:
    FAIL;
  }
  fprintf(f, "\n");

  return ip - bf->code_ptr - pos;
stop:
  fprintf(f, "<end>\n");
  return ip - bf->code_ptr - pos;
}

/* Disassembles the bytecode pool */
void disassemble(FILE *f, const bytefile *const bf) {
  bytecode op;
  int pos = 0;
  do {
    int size = disassemble_instruction(f, bf, pos, &op);
    if (op == STOP) {
      break;
    }
    pos += size;
  } while (1);
}

/* Dumps the contents of the file */
void dump_file(FILE *f, const bytefile *const bf) {
  int i;

  fprintf(f, "String table size       : %d\n", bf->stringtab_size);
  fprintf(f, "Global area size        : %d\n", bf->global_area_size);
  fprintf(f, "Number of public symbols: %d\n", bf->public_symbols_number);
  fprintf(f, "Public symbols          :\n");

  for (i = 0; i < bf->public_symbols_number; i++)
    fprintf(f, "   0x%.8x: %s\n", get_public_offset(bf, i),
            get_public_name(bf, i));

  fprintf(f, "Code:\n");
  disassemble(f, bf);
}
