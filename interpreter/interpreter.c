#include <stdio.h>

#include "../runtime/runtime.h"
#include "bytecode.h"
#include "interpreter.h"
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

void interpret_bc(FILE *f, interpreter_state_t *state) {
#define INT (read_i32(state))
#define BYTE (read_u8(state))
#define STRING (get_string(state->bf, INT))
#define FAIL failure("ERROR: invalid opcode %d-%d\n", h, l)

#ifdef DEBUG
#define DBG(...) fprintf(f, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

  char *base_ip = state->ip;
  char *ops[] = {
      "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "!!"};
  char *pats[] = {"=str", "#string", "#array", "#sexp", "#ref", "#val", "#fun"};
  char *lds[] = {"LD", "LDA", "ST"};

  do {
    char x = BYTE, h = (x & 0xF0) >> 4, l = x & 0x0F;

    DBG("0x%.8lx:\t", state->ip - base_ip - 1);

    switch (h) {
    case 15:
      goto stop;

    /* BINOP */
    case 0: {
      int32_t b = callstack_pop_operand(state->callstack);
      int32_t a = callstack_pop_operand(state->callstack);
      int32_t result;

      switch (l) {
      case 1:
        result = a + b;
        break; /* + */
      case 2:
        result = a - b;
        break; /* - */
      case 3:
        result = a * b;
        break; /* * */
      case 4:
        result = a / b;
        break; /* / */
      case 5:
        result = a % b;
        break; /* % */
      case 6:
        result = a < b;
        break; /* < */
      case 7:
        result = a <= b;
        break; /* <= */
      case 8:
        result = a > b;
        break; /* > */
      case 9:
        result = a >= b;
        break; /* >= */
      case 10:
        result = a == b;
        break; /* == */
      case 11:
        result = a != b;
        break; /* != */
      case 12:
        result = a && b;
        break; /* && */
      case 13:
        result = a || b;
        break; /* !! */
      default:
        fprintf(stderr, "Unsupported BINOP %d\n", l);
        exit(1);
      }

      DBG("%d %s %d = %d", a, ops[l - 1], b, result);
      callstack_push_operand(state->callstack, result);
    } break;

    case 1:
      switch (l) {
      case 0: /* CONST */
      {
        int32_t value = INT;
        DBG("CONST\t%d", value);
        callstack_push_operand(state->callstack, value);
      } break;

      case 1: /* STRING */
      {
        int32_t str_idx = INT;
        DBG("STRING IDX\t%d", str_idx);
        callstack_push_operand(state->callstack, str_idx);
      } break;

      case 2: /* SEXP */
      {
        char *tag = STRING;
        int32_t arity = INT;
        (void)tag;
        (void)arity;
        fprintf(stderr, "SEXP instruction not implemented yet\n");
        exit(1);
      } break;

      case 3: /* STI */
        fprintf(stderr, "STI instruction not implemented yet\n");
        exit(1);
        break;

      case 4: /* STA */
        fprintf(stderr, "STA instruction not implemented yet\n");
        exit(1);
        break;

      case 5: /* JMP */
      {
        int32_t offset = INT;
        DBG("JMP\t0x%.8x", offset);
        state->ip = base_ip + offset;
      } break;

      case 6: /* END */
      case 7: /* RET */
      {
        DBG("END");

        uint32_t callee_nargs = callstack_nargs(state->callstack);
        int32_t callee_ret = callstack_pop_operand(state->callstack);
        char *ret_ip = callstack_pop_frame(state->callstack);

        if (ret_ip == NULL) {
          goto stop;
        }

        for (uint32_t i = 0; i < callee_nargs; i++) {
          callstack_pop_operand(state->callstack);
        }

        callstack_push_operand(state->callstack, callee_ret);
        state->ip = ret_ip;
      } break;

      case 8: /* DROP */
        DBG("DROP");
        callstack_pop_operand(state->callstack);
        break;

      case 9: /* DUP */
      {
        int32_t value = callstack_pop_operand(state->callstack);
        DBG("DUP\t%d", value);
        callstack_push_operand(state->callstack, value);
        callstack_push_operand(state->callstack, value);
      } break;

      case 10: /* SWAP */
      {
        int32_t a = callstack_pop_operand(state->callstack);
        int32_t b = callstack_pop_operand(state->callstack);
        DBG("SWAP\t%d\t%d", a, b);
        callstack_push_operand(state->callstack, a);
        callstack_push_operand(state->callstack, b);
      } break;

      case 11: /* ELEM */
        fprintf(stderr, "ELEM instruction not implemented yet\n");
        exit(1);
        break;

      default:
        FAIL;
      }
      break;

    case 2: /* LD operations */
      switch (l) {
      case 0: /* LD G(m) */
      {
        int32_t index = INT;
        if (index >= 0 && (size_t)index < state->num_globals) {
          DBG("LD\tG(%d)", index);
          callstack_push_operand(state->callstack, state->globals[index]);
        } else {
          fprintf(stderr, "Invalid global variable index: %d\n", index);
          exit(1);
        }
      } break;

      case 1: /* LD L(m) */
      {
        int32_t index = INT;
        int32_t value = callstack_get_local(state->callstack, index);
        DBG("LD\tL(%d)", index);
        callstack_push_operand(state->callstack, value);
      } break;

      case 2: /* LD A(m) */
      {
        int32_t index = INT;
        int32_t value = callstack_get_arg(state->callstack, index);
        DBG("LD\tL(%d)", index);
        callstack_push_operand(state->callstack, value);
      } break;

      case 3: /* LD C(m) */
        fprintf(stderr, "Closure variables not implemented yet\n");
        exit(1);
        break;

      default:
        FAIL;
      }
      break;

    case 3: /* LDA operations - Load Address */
      switch (l) {
      case 0: /* LDA G(m) */
      case 1: /* LDA L(m) */
      case 2: /* LDA A(m) */
      case 3: /* LDA C(m) */
      {
        int32_t index = INT;
        (void)index;
        fprintf(stderr, "Load address operations not implemented yet\n");
        exit(1);
      } break;
      default:
        FAIL;
      }
      break;

    case 4: /* ST operations */
      switch (l) {
      case 0: /* ST G(m) */
      {
        int32_t index = INT;
        int32_t value = callstack_pop_operand(state->callstack);
        if (index >= 0 && (size_t)index < state->num_globals) {
          DBG("ST\tG(%d)", index);
          state->globals[index] = value;
          callstack_push_operand(state->callstack, value);
        } else {
          fprintf(stderr, "Invalid global variable index: %d\n", index);
          exit(1);
        }
      } break;

      case 1: /* ST L(m) */
      {
        int32_t index = INT;
        int32_t value = callstack_pop_operand(state->callstack);
        DBG("ST\tL(%d)", index);
        callstack_set_local(state->callstack, index, value);
        callstack_push_operand(state->callstack, value);
      } break;

      case 2: /* ST A(m) */
      {
        int32_t index = INT;
        int32_t value = callstack_pop_operand(state->callstack);
        DBG("ST\tA(%d)", index);
        callstack_set_arg(state->callstack, index, value);
        callstack_push_operand(state->callstack, value);
      } break;

      case 3: /* ST C(m) */
        fprintf(stderr, "Closure variables not implemented yet\n");
        exit(1);
        break;

      default:
        FAIL;
      }
      break;

    case 5:
      switch (l) {
      case 0: /* CJMPz */
      {
        int32_t l_offset = INT;
        DBG("CJMPz\t0x%.8x", l_offset);

        int32_t value = callstack_pop_operand(state->callstack);
        if (!value)
          state->ip = base_ip + l_offset;
      } break;

      case 1: /* CJMPnz */
      {
        int32_t l_offset = INT;
        DBG("CJMPnz\t0x%.8x", l_offset);

        // Jump if operand non-zero
        int32_t value = callstack_pop_operand(state->callstack);
        if (value)
          state->ip = base_ip + l_offset;
      } break;

      case 2: /* BEGIN */
      {
        int nargs = INT;
        int nlocals = INT;

        if (callstack_nframes(state->callstack) == 0)
          callstack_push_frame(state->callstack, NULL, nargs);

        assert(nargs == callstack_nargs(state->callstack));

        DBG("BEGIN\t%d\t%d", nargs, nlocals);
        callstack_alloc_locals(state->callstack, nlocals);
      } break;

      case 3: /* CBEGIN */
        fprintf(stderr, "CBEGIN instruction not implemented yet\n");
        exit(1);
        break;

      case 4:
        DBG("CLOSURE\t0x%.8x", INT);
        {
          int n = INT;
          for (int i = 0; i < n; i++) {
            switch (BYTE) {
            case 0:
              DBG("G(%d)", INT);
              break;
            case 1:
              DBG("L(%d)", INT);
              break;
            case 2:
              DBG("A(%d)", INT);
              break;
            case 3:
              DBG("C(%d)", INT);
              break;
            default:
              FAIL;
            }
          }
        };
        break;

      case 5: /* CALLC */
        fprintf(stderr, "CALLC instruction not implemented yet\n");
        exit(1);
        break;

      case 6: /* CALL */
      {
        int offset = INT;
        int nargs = INT;
        char *ret_ip = state->ip;

        callstack_push_frame(state->callstack, ret_ip, nargs);
        state->ip = base_ip + offset;
      } break;

      case 7: /* TAG */
      {
        char *tag = STRING;
        int arity = INT;
        (void)tag;
        (void)arity;
        fprintf(stderr, "TAG instruction not implemented yet\n");
        exit(1);
      } break;

      case 8: /* ARRAY */
      {
        int size = INT;
        (void)size;
        fprintf(stderr, "ARRAY instruction not implemented yet\n");
        exit(1);
      } break;

      case 9: /* FAIL */
      {
        int line = INT;
        int col = INT;
        fprintf(stderr, "Pattern matching failure at %d:%d\n", line, col);
        exit(1);
      } break;

      case 10: /* LINE */
      {
        int line = INT;
        DBG("LINE\t%d", line);
      } break;

      default:
        FAIL;
      }
      break;

    case 6: /* Pattern matching */
      fprintf(stderr, "Pattern matching not implemented yet\n");
      exit(1);
      break;

    case 7: /* Built-in functions */
    {
      switch (l) {
      case 0: /* Lread */
      {
        int32_t value;
        fprintf(stdout, " > ");
        if (scanf("%d", &value) == 1) {
          callstack_push_operand(state->callstack, value);
        } else {
          fprintf(stderr, "Failed to read integer\n");
          exit(1);
        }
      } break;

      case 1: /* Lwrite */
      {
        int32_t value = callstack_pop_operand(state->callstack);
        printf("%d\n", value); /* НЕ под DEBUG */
        callstack_push_operand(state->callstack, value);
      } break;

      case 2: /* Llength */
        fprintf(stderr, "Llength not implemented yet\n");
        exit(1);
        break;

      case 3: /* Lstring */
        fprintf(stderr, "Lstring not implemented yet\n");
        exit(1);
        break;

      case 4: /* Barray */
      {
        int length = INT;
        (void)length;
        fprintf(stderr, "Barray not implemented yet\n");
        exit(1);
      } break;

      default:
        FAIL;
      }
    } break;

    default:
      FAIL;
    }

    DBG("\n");
  } while (1);

stop:
  DBG("<end>\n");
}