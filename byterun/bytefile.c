/* Lama SM Bytecode interpreter */

#include "../runtime/runtime.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytefile.h"
#include "disasm.h"

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
                            bytecode *ret_opcode, uint32_t *inc,
                            uint32_t *dec) {
  if ((size_t)pos >= bf->code_size)
    return -1;
  char *ip = bf->code_ptr + pos;
  char *ops[] = {
      "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "!!"};
  char *pats[] = {"=str", "#string", "#array", "#sexp", "#ref", "#val", "#fun"};
  char *lds[] = {"LD", "LDA", "ST"};

  unsigned char x = BYTE, h = (x & 0xF0) >> 4, l = x & 0x0F;

  if (ret_opcode)
    *ret_opcode = x;
  if (inc)
    *inc = 0;
  if (dec)
    *dec = 0;

  fprintf(f, "0x%.8x:\t", pos);

  switch (h) {
  case 15:
    fprintf(f, "STOP %d", x);
    break;

  /* BINOP */
  case 0:
    fprintf(f, "BINOP\t%s", ops[l - 1]);
    if (dec)
      *dec = 2;
    if (inc)
      *inc = 1;
    break;

  case 1:
    switch (l) {
    case 0:
      fprintf(f, "CONST\t%d", INT);
      if (inc)
        *inc = 1;
      break;

    case 1:
      fprintf(f, "STRING\t%s", get_string(bf, INT));
      if (inc)
        *inc = 1;
      break;

    case 2:
      fprintf(f, "SEXP\t%s ", get_string(bf, INT));
      int n = INT;
      fprintf(f, "%d", n);
      if (dec)
        *dec = n;
      if (inc)
        *inc = 1;
      break;

    case 3:
      fprintf(f, "STI");
      if (dec)
        *dec = 2;
      if (inc)
        *inc = 1;
      break;

    case 4:
      fprintf(f, "STA");
      break;

    case 5:
      fprintf(f, "JMP\t0x%.8x", INT);
      break;

    case 6:
      fprintf(f, "END");
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 1;
      break;

    case 7:
      fprintf(f, "RET");
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 1;
      break;

    case 8:
      fprintf(f, "DROP");
      if (dec)
        *dec = 1;
      break;

    case 9:
      fprintf(f, "DUP");
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 2;
      break;

    case 10:
      fprintf(f, "SWAP");
      if (dec)
        *dec = 2;
      if (inc)
        *inc = 2;
      break;

    case 11:
      fprintf(f, "ELEM");
      if (dec)
        *dec = 2;
      if (inc)
        *inc = 1;
      break;

    default:
      failure("ERROR: invalid opcode %d-%d\n", h, l);
    }
    break;

  case 4:
    if (dec)
      *dec += 1;
  case 2:
  case 3:
    fprintf(f, "%s\t", lds[h - 2]);
    if (inc)
      *inc += 1;
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
      failure("ERROR: invalid opcode %d-%d\n", h, l);
    }
    break;

  case 5:
    switch (l) {
    case 0:
      fprintf(f, "CJMPz\t0x%.8x", INT);
      if (dec)
        *dec = 1;
      break;

    case 1:
      fprintf(f, "CJMPnz\t0x%.8x", INT);
      if (dec)
        *dec = 1;
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
      if (inc)
        *inc = 1;
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
            failure("ERROR: invalid opcode %d-%d\n", h, l);
          }
        }
      };
      break;

    case 5: {
      int n = INT;
      fprintf(f, "CALLC\t%d", n);
      if (dec)
        *dec = n + 1;
      if (inc)
        *inc = 1;
    } break;

    case 6:
      fprintf(f, "CALL\t0x%.8x ", INT);
      int n = INT;
      fprintf(f, "%d", n);
      if (dec)
        *dec = n;
      if (inc)
        *inc = 1;
      break;

    case 7:
      fprintf(f, "TAG\t%s ", get_string(bf, INT));
      fprintf(f, "%d", INT);
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 1;
      break;

    case 8:
      fprintf(f, "ARRAY\t%d", INT);
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 1;
      break;

    case 9:
      fprintf(f, "FAIL\t%d", INT);
      fprintf(f, "%d", INT);
      if (dec)
        *dec = 1;
      break;

    case 10:
      fprintf(f, "LINE\t%d", INT);
      break;

    default:
      failure("ERROR: invalid opcode %d-%d\n", h, l);
    }
    break;

  case 6:
    fprintf(f, "PATT\t%s", pats[l]);
    if (dec)
      *dec = (l == 0) ? 2 : 1; /* Patt =str (60) takes 2 operands */
    if (inc)
      *inc = 1;
    break;

  case 7: {
    switch (l) {
    case 0:
      fprintf(f, "CALL\tLread");
      if (inc)
        *inc = 1;
      break;

    case 1:
      fprintf(f, "CALL\tLwrite");
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 1;
      break;

    case 2:
      fprintf(f, "CALL\tLlength");
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 1;
      break;

    case 3:
      fprintf(f, "CALL\tLstring");
      if (dec)
        *dec = 1;
      if (inc)
        *inc = 1;
      break;

    case 4: {
      int n = INT;
      fprintf(f, "CALL\tBarray\t%d", n);
      if (dec)
        *dec = n;
      if (inc)
        *inc = 1;
      break;
    }

    default:
      failure("ERROR: invalid opcode %d-%d\n", h, l);
    }
  } break;

  default:
    failure("ERROR: invalid opcode %d-%d\n", h, l);
  }

  fprintf(f, "\n");

  return ip - bf->code_ptr - pos;
}

/* Disassembles the bytecode pool */
void disassemble(FILE *f, const bytefile *const bf) {
  bytecode op;
  int pos = 0;
  do {
    uint32_t inc = 0, dec = 0;
    int size = disassemble_instruction(f, bf, pos, &op, &inc, &dec);
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
