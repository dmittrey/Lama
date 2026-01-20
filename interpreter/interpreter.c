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

      case 1: /* STRING */
        {
          int32_t str_idx = INT;

          fprintf(f, "STRING IDX\t%d", str_idx);
          callstack_push_operand(state->callstack, str_idx);
        }
        break;

      case 2: /* SEXP */
        {
          char *tag = STRING;
          int32_t arity = INT;
          /* TODO: Implement S-expression handling */
          fprintf(stderr, "SEXP instruction not implemented yet\n");
          exit(1);
        }
        break;

      case 3: /* STI */
        /* TODO: Implement store indirect */
        fprintf(stderr, "STI instruction not implemented yet\n");
        exit(1);
        break;

      case 4: /* STA */
        /* TODO: Implement store array */
        fprintf(stderr, "STA instruction not implemented yet\n");
        exit(1);
        break;

      case 5: /* JMP */
        {
          int32_t offset = INT;

          fprintf(f, "JMP\t0x%.8x", offset);
          state->ip = base_ip + offset;
        }
        break;

      case 6: /* END */
        /* Marks the end of the procedure definition. When execut- −1, +1 ed, returns the top value to the caller of this procedure. */
        /* TODO: Implement END procedure */
        fprintf(stderr, "END instruction not implemented yet\n");
        exit(1);

      case 7: /* RET */
        /* TODO: Implement RET procedure */
        fprintf(stderr, "RET returns the top value to the caller of this procedure\n");
        exit(1);
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

      case 11: /* ELEM */
        /* TODO: Implement array element access */
        fprintf(stderr, "ELEM instruction not implemented yet\n");
        exit(1);
        break;

      default:
        FAIL;
      }
      break;

    case 2: /* LD operations */
      switch (l)
      {
      case 0: /* LD G(m) */
        {
          int32_t index = INT;
          if (index >= 0 && (size_t)index < state->num_globals) {
            fprintf(f, "LD\tG(%d)", index);
            callstack_push_operand(state->callstack, state->globals[index]);
          } else {
            fprintf(stderr, "Invalid global variable index: %d\n", index);
            exit(1);
          }
        }
        break;
      case 1: /* LD L(m) */
        {
          int32_t index = INT;
          int32_t value = callstack_get_local(state->callstack, index);
          
          fprintf(f, "LD\tL(%d)", index);
          callstack_push_operand(state->callstack, value);
        }
        break;
      case 2: /* LD A(m) */
        {
          /* TODO: Implement args variables */
          fprintf(stderr, "Args not implemented yet\n");
          exit(1);
        }
        break;
      case 3: /* LD C(m) */
        {
          int32_t index = INT;
          /* TODO: Implement closure variables */
          fprintf(stderr, "Closure variables not implemented yet\n");
          exit(1);
        }
        break;
      default:
        FAIL;
      }
      break;

    case 3: /* LDA operations - Load Address */
      switch (l)
      {
      case 0: /* LDA G(m) */
      case 1: /* LDA L(m) */
      case 2: /* LDA A(m) */
      case 3: /* LDA C(m) */
        {
          int32_t index = INT;
          /* TODO: Implement load address operations */
          fprintf(stderr, "Load address operations not implemented yet\n");
          exit(1);
        }
        break;
      default:
        FAIL;
      }
      break;

    case 4: /* ST operations */
      switch (l)
      {
      case 0: /* ST G(m) */
        {
          int32_t index = INT;
          int32_t value = callstack_pop_operand(state->callstack);
          if (index >= 0 && (size_t)index < state->num_globals) {
            fprintf(f, "ST\tG(%d)", index);
            state->globals[index] = value;
            callstack_push_operand(state->callstack, value); /* Push back onto stack */
          } else {
            fprintf(stderr, "Invalid global variable index: %d\n", index);
            exit(1);
          }
        }
        break;
      case 1: /* ST L(m) */
        {
          int32_t index = INT;
          int32_t value = callstack_pop_operand(state->callstack);

          fprintf(f, "ST\tL(%d)", index);
          callstack_set_local(state->callstack, index, value);
          callstack_push_operand(state->callstack, value); /* Push back onto stack */
        }
        break;
      case 2: /* ST A(m) */
        {
          /* TODO: Implement load address operations */
          fprintf(stderr, "Implement me\n");
          exit(1);
        }
        break;
      case 3: /* ST C(m) */
        {
          /* TODO: Implement closure variables */
          fprintf(stderr, "Closure variables not implemented yet\n");
          exit(1);
        }
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

      case 2: /* BEGIN */
        {
          int nargs = INT;
          int nlocals = INT;
          
          fprintf(f, "BEGIN\t%d\t%d", nargs, nlocals);
          callstack_push_frame(state->callstack, nargs, nlocals);
        }
        break;

      case 3: /* CBEGIN */
        {
          /* TODO: Create function frame with closure */
          fprintf(stderr, "CBEGIN instruction not implemented yet\n");
          exit(1);
        }
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

      case 5: /* CALLC */
        {
          /* TODO: Call closure */
          fprintf(stderr, "CALLC instruction not implemented yet\n");
          exit(1);
        }
        break;

      case 6: /* CALL */
        {
          int offset = INT;
          int nargs = INT;
          /* TODO: Call function */
          fprintf(stderr, "CALL instruction not implemented yet\n");
          exit(1);
        }
        break;

      case 7: /* TAG */
        {
          char *tag = STRING;
          int arity = INT;
          /* TODO: Check S-expression tag */
          fprintf(stderr, "TAG instruction not implemented yet\n");
          exit(1);
        }
        break;

      case 8: /* ARRAY */
        {
          int size = INT;
          /* TODO: Check array size */
          fprintf(stderr, "ARRAY instruction not implemented yet\n");
          exit(1);
        }
        break;

      case 9: /* FAIL */
        {
          int line = INT;
          int col = INT;
          /* TODO: Pattern matching failure */
          fprintf(stderr, "Pattern matching failure at %d:%d\n", line, col);
          exit(1);
        }
        break;

      case 10: /* LINE */
        {
          int line = INT;
          fprintf(f, "LINE\t%d", line);
        }
        break;

      default:
        FAIL;
      }
      break;

    case 6: /* Pattern matching */
      /* TODO: Implement pattern matching operations */
      fprintf(stderr, "Pattern matching not implemented yet\n");
      exit(1);
      break;

    case 7: /* Built-in functions */
    {
      switch (l)
      {
      case 0: /* Lread */
        {
          int32_t value;
          if (scanf("%d", &value) == 1) {
            callstack_push_operand(state->callstack, value);
          } else {
            fprintf(stderr, "Failed to read integer\n");
            exit(1);
          }
        }
        break;

      case 1: /* Lwrite */
        {
          int32_t value = callstack_pop_operand(state->callstack);
          printf("%d\n", value);
          callstack_push_operand(state->callstack, value);  /* Leave value on stack */
        }
        break;

      case 2: /* Llength */
        /* TODO: Implement length */
        fprintf(stderr, "Llength not implemented yet\n");
        exit(1);
        break;

      case 3: /* Lstring */
        /* TODO: Implement string conversion */
        fprintf(stderr, "Lstring not implemented yet\n");
        exit(1);
        break;

      case 4: /* Barray */
        {
          int length = INT;
          /* TODO: Create array */
          fprintf(stderr, "Barray not implemented yet\n");
          exit(1);
        }
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