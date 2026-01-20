#include <stdio.h>

#include "../runtime/runtime.h"
#include "interpreter.h"
#include "bytecode.h"
#include "state.h"

static inline uint8_t read_u8(interpreter_state_t *st) {
    return (uint8_t)*st->ip++;
}

static inline int32_t read_i32(interpreter_state_t *st) {
    int32_t v;
    memcpy(&v, st->ip, sizeof(v));
    st->ip += sizeof(v);
    return v;
}

void interpret_bc(FILE *f, interpreter_state_t *state)
{
#define INT    (read_i32(state))
#define BYTE   (read_u8(state))
#define STRING (get_string(state->bf, INT))
#define FAIL failure("ERROR: invalid opcode %d-%d\n", h, l)

    char* base_ip = state->ip;
    char *ops[] = {"+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "!!"};
    char *pats[] = {"=str", "#string", "#array", "#sexp", "#ref", "#val", "#fun"};
    char *lds[] = {"LD", "LDA", "ST"};
    do
    {
    char x = BYTE,
         h = (x & 0xF0) >> 4,
         l = x & 0x0F;

    fprintf(f, "0x%.8lx:\t", state->ip - base_ip - 1);

    switch (h)
    {
    case 15:
      goto stop;

    /* BINOP */
    case 0:
      {
        int32_t b = callstack_pop_operand(state->callstack);
        int32_t a = callstack_pop_operand(state->callstack);
        int32_t result;

        switch (l) {
          case 1: result = a + b; break;  /* + */
          case 2: result = a - b; break;  /* - */
          case 3: result = a * b; break;  /* * */
          case 4: result = a / b; break;  /* / */
          case 5: result = a % b; break;  /* % */
          case 6: result = a < b; break;  /* < */
          case 7: result = a <= b; break;  /* <= */
          case 8: result = a > b; break;  /* > */
          case 9: result = a >= b; break;  /* >= */
          case 10: result = a == b; break;  /* == */
          case 11: result = a != b; break;  /* != */
          case 12: result = a && b; break;  /* && */
          case 13: result = a || b; break;  /* !! */
          default:
            fprintf(stderr, "Unsupported BINOP %d\n", l);
            exit(1);
        }

        fprintf(f, "%d %s %d = %d", a, ops[l-1], b, result);
        callstack_push_operand(state->callstack, result);
      }
      break;

    case 1:
      switch (l)
      {
      case 0: /* CONST */
        {
          int32_t value = INT;

          fprintf(f, "CONST\t%d", value);
          callstack_push_operand(state->callstack, value);
        }
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

      case 5: /* JMP */
        {
          int32_t offset = INT;

          fprintf(f, "JMP\t0x%.8x", offset);
          state->ip = base_ip + offset;
        }
        break;

      case 6:
        fprintf(f, "END");
        break;

      case 7:
        fprintf(f, "RET");
        break;

      case 8: /* DROP */
        fprintf(f, "DROP");
        callstack_pop_operand(state->callstack);
        break;

      case 9: /* DUP */
        {
          int32_t value = callstack_pop_operand(state->callstack);

          fprintf(f, "DUP\t%d", value);
          callstack_push_operand(state->callstack, value);
          callstack_push_operand(state->callstack, value);
        }
        break;

      case 10: /* SWAP */
        {
          int32_t a = callstack_pop_operand(state->callstack);
          int32_t b = callstack_pop_operand(state->callstack);

          fprintf(f, "SWAP\t%d\t%d", a, b);
          callstack_push_operand(state->callstack, a);
          callstack_push_operand(state->callstack, b);
        }
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
      switch (l)
      {
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
      switch (l)
      {
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
          for (int i = 0; i < n; i++)
          {
            switch (BYTE)
            {
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

    case 7:
    {
      switch (l)
      {
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
    }
    break;

    default:
      FAIL;
    }

    fprintf(f, "\n");
  } while (1);
stop:
  fprintf(f, "<end>\n");
}