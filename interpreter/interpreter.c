#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/runtime.h"
#include "bytecode.h"
#include "callstack.h"
#include "interpreter.h"
#include "state.h"

extern aint Lread(void);
extern aint Lwrite(aint n);
extern aint Llength(void *p);
extern void *Lstring(aint *args);
extern void *Barray(aint *args, aint bn);

extern aint Ls__Infix_3333(void *p, void *q); /* !! */
extern aint Ls__Infix_3361(void *p, void *q); /* != */
extern aint Ls__Infix_3838(void *p, void *q); /* && */
extern aint Ls__Infix_37(void *p, void *q);   /* %  */
extern aint Ls__Infix_42(void *p, void *q);   /* *  */
extern aint Ls__Infix_43(void *p, void *q);   /* +  */
extern aint Ls__Infix_45(void *p, void *q);   /* -  */
extern aint Ls__Infix_47(void *p, void *q);   /* /  */
extern aint Ls__Infix_60(void *p, void *q);   /* <  */
extern aint Ls__Infix_6061(void *p, void *q); /* <= */
extern aint Ls__Infix_6161(void *p, void *q); /* == */
extern aint Ls__Infix_62(void *p, void *q);   /* >  */
extern aint Ls__Infix_6261(void *p, void *q); /* >= */

static inline uint8_t read_u8(interpreter_state_t *st) {
  return (uint8_t)*st->ip++;
}

static inline int32_t read_i32(interpreter_state_t *st) {
  int32_t v;
  memcpy(&v, st->ip, sizeof(v));
  st->ip += sizeof(v);
  return v;
}

static inline bool as_bool(aint v) { return UNBOXED(v) ? (UNBOX(v) != 0) : 1; }

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
      aint b = callstack_pop_operand(state->callstack);
      aint a = callstack_pop_operand(state->callstack);
      int64_t boxed_res;

      switch (l) {
      case 1:
        boxed_res = Ls__Infix_43((void *)a, (void *)b);
        break; /* + */
      case 2:
        boxed_res = Ls__Infix_45((void *)a, (void *)b);
        break; /* - */
      case 3:
        boxed_res = Ls__Infix_42((void *)a, (void *)b);
        break; /* * */
      case 4:
        boxed_res = Ls__Infix_47((void *)a, (void *)b);
        break; /* / */
      case 5:
        boxed_res = Ls__Infix_37((void *)a, (void *)b);
        break; /* % */
      case 6:
        boxed_res = Ls__Infix_60((void *)a, (void *)b);
        break; /* < */
      case 7:
        boxed_res = Ls__Infix_6061((void *)a, (void *)b);
        break; /* <= */
      case 8:
        boxed_res = Ls__Infix_62((void *)a, (void *)b);
        break; /* > */
      case 9:
        boxed_res = Ls__Infix_6261((void *)a, (void *)b);
        break; /* >= */
      case 10:
        boxed_res = Ls__Infix_6161((void *)a, (void *)b);
        break; /* == */
      case 11:
        boxed_res = Ls__Infix_3361((void *)a, (void *)b);
        break; /* != */
      case 12:
        boxed_res = Ls__Infix_3838((void *)a, (void *)b);
        break; /* && */
      case 13:
        boxed_res = Ls__Infix_3333((void *)a, (void *)b);
        break; /* !! */
      default:
        fprintf(stderr, "Unsupported BINOP %d\n", l);
        exit(1);
      }

      DBG("%" PRIdAI " %s %" PRIdAI " = %" PRIdAI, UNBOX(a), ops[l - 1],
          UNBOX(b), UNBOX(boxed_res));
      callstack_push_operand(state->callstack, boxed_res);
    } break;

    case 1:
      switch (l) {
      case 0: /* CONST */
      {
        int32_t value = INT;
        DBG("CONST\t%d", value);
        callstack_push_operand(state->callstack, BOX(value));
      } break;

      case 1: /* STRING */
      {
        int32_t str_idx = INT;
        DBG("STRING IDX\t%d", str_idx);
        // TODO alloca in GC heap
        callstack_push_operand(state->callstack, BOX(str_idx));
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
        DBG("END/RET");

        uint32_t callee_nargs = callstack_nargs(state->callstack);
        aint callee_ret = callstack_pop_operand(state->callstack);

        uint32_t ret_off = callstack_pop_frame(state->callstack);

        // main frame exit
        if (ret_off == 0) {
          goto stop;
        }

        for (uint32_t i = 0; i < callee_nargs; i++) {
          callstack_pop_operand(state->callstack);
        }

        callstack_push_operand(state->callstack, callee_ret);
        state->ip = base_ip + ret_off;
      } break;

      case 8: /* DROP */
        DBG("DROP");
        callstack_pop_operand(state->callstack);
        break;

      case 9: /* DUP */
      {
        aint v = callstack_pop_operand(state->callstack);
        DBG("DUP");
        // TODO Lclone for agregates??
        callstack_push_operand(state->callstack, v);
        callstack_push_operand(state->callstack, v);
      } break;

      case 10: /* SWAP */
      {
        aint a = callstack_pop_operand(state->callstack);
        aint b = callstack_pop_operand(state->callstack);
        DBG("SWAP");
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
        aint value = callstack_get_local(state->callstack, (uint32_t)index);
        DBG("LD\tL(%d)", index);
        callstack_push_operand(state->callstack, value);
      } break;

      case 2: /* LD A(m) */
      {
        int32_t index = INT;
        aint value = callstack_get_arg(state->callstack, (uint32_t)index);
        DBG("LD\tA(%d)", index);
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
        aint value = callstack_pop_operand(state->callstack);
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
        aint value = callstack_pop_operand(state->callstack);
        DBG("ST\tL(%d)", index);
        callstack_set_local(state->callstack, (uint32_t)index, value);
        callstack_push_operand(state->callstack, value);
      } break;

      case 2: /* ST A(m) */
      {
        int32_t index = INT;
        aint value = callstack_pop_operand(state->callstack);
        DBG("ST\tA(%d)", index);
        callstack_set_arg(state->callstack, (uint32_t)index, value);
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

        aint value = callstack_pop_operand(state->callstack);
        if (!as_bool(value)) {
          state->ip = base_ip + l_offset;
        }
      } break;

      case 1: /* CJMPnz */
      {
        int32_t l_offset = INT;
        DBG("CJMPnz\t0x%.8x", l_offset);

        aint value = callstack_pop_operand(state->callstack);
        if (as_bool(value)) {
          state->ip = base_ip + l_offset;
        }
      } break;

      case 2: /* BEGIN */
      {
        int nargs = INT;
        int nlocals = INT;

        if (callstack_nframes(state->callstack) == 0)
          callstack_push_frame(state->callstack, 0, (uint32_t)nargs);

        assert((uint32_t)nargs == callstack_nargs(state->callstack));

        DBG("BEGIN\t%d\t%d", nargs, nlocals);
        callstack_alloc_locals(state->callstack, (uint32_t)nlocals);
      } break;

      case 3: /* CBEGIN */
        fprintf(stderr, "CBEGIN instruction not implemented yet\n");
        exit(1);
        break;

      case 4: /* CLOSURE */
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
        uint32_t ret_off = (uint32_t)(state->ip - base_ip);

        callstack_push_frame(state->callstack, ret_off, (uint32_t)nargs);
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
        fprintf(stdout, " ");
        aint v = Lread();
        callstack_push_operand(state->callstack, v);
      } break;

      case 1: /* Lwrite */
      {
        aint v = callstack_pop_operand(state->callstack);
        Lwrite(v);
        callstack_push_operand(state->callstack, v);
      } break;

      case 2: /* Llength */
      {
        aint v = callstack_pop_operand(state->callstack);
        aint r = Llength((void *)v);
        callstack_push_operand(state->callstack, r);
      } break;

      case 3: /* Lstring */
      {
        aint v = callstack_pop_operand(state->callstack);

        void *s = Lstring(&v);
        callstack_push_operand(state->callstack, (aint)s);
      } break;

      case 4: /* Barray */
      {
        int n = INT;
        if (n < 0) {
          fprintf(stderr, "Barray: negative length %d\n", n);
          exit(1);
        }

        aint *elems = callstack_n_operands_sequence(state->callstack, n);
        void *a = Barray(elems, n);
        for (size_t i = 0; i < n; i++)
          callstack_pop_operand(state->callstack);
        callstack_push_operand(state->callstack, (aint)a);
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