#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../runtime/gc.h"
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

extern void *Bclosure(aint *args, aint bn);
extern void *Bstring(aint *args);
extern void *Bsexp(aint *args, aint bn);
extern void *Bsta(void *x, aint i, void *v);
extern void *Belem(void *p, aint i);
extern aint LtagHash(char *s);
extern aint Btag(void *d, aint t, aint n);

extern aint Bstring_patt(void *x, void *y);
extern aint Barray_patt(void *d, aint n);
extern aint Bstring_tag_patt(void *x);
extern aint Barray_tag_patt(void *x);
extern aint Bsexp_tag_patt(void *x);
extern aint Bclosure_tag_patt(void *x);

extern void Bmatch_failure(void *v, char *fname, aint line, aint col);

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

static inline void *closure_entry_ptr(aint clos) {
  if (UNBOXED(clos)) {
    failure("CALLC/LD C: closure expected, got unboxed\n");
  }

  data *d = TO_DATA((void *)clos);
  if (TAG(d->data_header) != CLOSURE_TAG) {
    failure("CALLC/LD C: closure expected, got tag=%ld\n", TAG(d->data_header));
  }

  return ((void **)d->contents)[0];
}

static inline aint closure_capture_ref(aint clos, uint32_t idx) {
  if (UNBOXED(clos)) {
    failure("LD/ST/LDA C: closure expected, got unboxed\n");
  }

  data *d = TO_DATA((void *)clos);
  if (TAG(d->data_header) != CLOSURE_TAG) {
    failure("LD/ST/LDA C: closure expected, got tag=%ld\n",
            TAG(d->data_header));
  }

  aint len = LEN(d->data_header); // len = n + 1
  if ((aint)(idx + 1) >= len) {
    failure("LD/ST/LDA C: captured index %u out of range (len=%ld)\n", idx,
            len);
  }

  return ((aint *)d->contents)[idx + 1];
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
        char *str = STRING;
        DBG("STRING (%s)\n", str);

        void *r = Bstring((aint *)&str); // Allocate string in GC heap same as
                                         // in string table in .bc
        callstack_push_operand(state->callstack, (aint)r);
      } break;

      case 2: /* SEXP */
      {
        char *tag = STRING;
        int32_t arity = INT;
        DBG("SEXP (%s)\n", tag);

        aint th = LtagHash(tag);
        callstack_push_operand(state->callstack, th);

        if (arity == 0) /* constructor constant */
          break;

        // arg0 , ... , argN-1 , tag
        void *r = Bsexp(
            callstack_n_last_operands_sequence(state->callstack, arity + 1),
            BOX(arity + 1));

        callstack_pop_n_operands(state->callstack, arity + 1 /* With tag*/);
        callstack_push_operand(state->callstack, (aint)r);
      } break;

      case 3: /* STI */
      {
        aint val = callstack_pop_operand(state->callstack);
        aint ref = callstack_pop_operand(state->callstack);
        DBG("STI\n");

        aint *addr = callstack_resolve_ref(state->callstack, ref);
        *addr = val;

        callstack_push_operand(state->callstack, val);
      } break;

      case 4: /* STA */
      {
        aint val = callstack_pop_operand(state->callstack);
        aint sec_op = callstack_pop_operand(state->callstack);
        DBG("STA\n");

        if (UNBOXED(sec_op)) {
          /* aggregate: agg idx val */
          aint idx = sec_op;
          aint agg = callstack_pop_operand(state->callstack);
          (void)Bsta((void *)agg, idx, (void *)val);
        } else {
          /* ref: ref val */
          aint ref = sec_op;
          aint *addr = callstack_resolve_ref(state->callstack, ref);
          *addr = val;
        }

        callstack_push_operand(state->callstack, val);
      } break;

      case 5: /* JMP */
      {
        int32_t offset = INT;
        DBG("JMP\t0x%.8x", offset);
        state->ip = base_ip + offset;
      } break;

      case 6: /* END */
      case 7: /* RET */
      {
        DBG("END/RET\n");

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
      {
        aint idx = callstack_pop_operand(state->callstack);
        aint agg = callstack_pop_operand(state->callstack);
        DBG("ELEM\t(idx=%lld)\n", UNBOX(idx));
        void *r = Belem((void *)agg, idx);
        callstack_push_operand(state->callstack, (aint)r);
      } break;

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
      {
        int32_t index = INT;
        aint clos = callstack_closure(state->callstack);
        if (UNBOXED(clos) && UNBOX(clos) == 0) {
          failure("LD C(%d): no closure in current frame\n", index);
        }

        aint ref = closure_capture_ref(clos, (uint32_t)index);
        aint *cell = callstack_resolve_ref(state->callstack, ref);
        callstack_push_operand(state->callstack, *cell);
      } break;

      default:
        FAIL;
      }
      break;

    case 3: /* LDA operations - Load Address */
      switch (l) {
      case 0: /* LDA G(m) */
      {
        int32_t index = INT;
        assert(index >= 0 && index < state->num_globals);
        aint *ref = (aint *)&state->globals[index];
        callstack_push_operand(state->callstack, (aint)ref);
      } break;
      case 1: /* LDA L(m) */
      {
        int32_t index = INT;
        aint *refp = callstack_local_addr(state->callstack, (uint32_t)index);
        callstack_push_operand(state->callstack, (aint)refp);
      } break;
      case 2: /* LDA A(m) */
      {
        int32_t index = INT;
        aint *refp = callstack_arg_addr(state->callstack, (uint32_t)index);
        callstack_push_operand(state->callstack, (aint)refp);
      } break;
      case 3: /* LDA C(m) */
      {
        int32_t index = INT;
        aint clos = callstack_closure(state->callstack);
        if (UNBOXED(clos) && UNBOX(clos) == 0) {
          failure("LDA C(%d): no closure in current frame\n", index);
        }

        aint ref = closure_capture_ref(clos, (uint32_t)index);
        callstack_push_operand(state->callstack, ref); // direct-address ref
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
      {
        int32_t index = INT;
        aint value = callstack_pop_operand(state->callstack);

        aint clos = callstack_closure(state->callstack);
        if (UNBOXED(clos) && UNBOX(clos) == 0) {
          failure("ST C(%d): no closure in current frame\n", index);
        }

        aint ref = closure_capture_ref(clos, (uint32_t)index);
        aint *cell = callstack_resolve_ref(state->callstack, ref);
        *cell = value;

        callstack_push_operand(state->callstack, value);
      } break;

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
      {
        int nargs = INT;
        int nlocals = INT;

        if (callstack_nframes(state->callstack) == 0)
          callstack_push_frame(state->callstack, 0, (uint32_t)nargs);

        assert((uint32_t)nargs == callstack_nargs(state->callstack));

        DBG("CBEGIN\t%d\t%d", nargs, nlocals);
        callstack_alloc_locals(state->callstack, (uint32_t)nlocals);
      } break;

      case 4: /* CLOSURE */
      {
        int32_t l_offset = INT;
        DBG("CLOSURE\t0x%.8x", l_offset);

        int n = INT;

        // args[0] = entry pointer, args[1..n] = captured refs
        aint *args = alloca(sizeof(aint) * (n + 1));
        args[0] = (aint)(base_ip + l_offset);

        for (int i = 0; i < n; i++) {
          switch (BYTE) {
          case 0: { // G(m)
            uint32_t index = INT;
            DBG(" G(%d)", index);
            args[i + 1] = (aint)&state->globals[index];
          } break;

          case 1: { // L(m)
            uint32_t index = INT;
            DBG(" L(%d)", index);
            aint ref = (aint)callstack_local_addr(state->callstack, index);
            args[i + 1] = (aint)callstack_resolve_ref(state->callstack, ref);
          } break;

          case 2: { // A(m)
            uint32_t index = INT;
            DBG(" A(%d)", index);
            aint ref = (aint)callstack_arg_addr(state->callstack, index);
            args[i + 1] = (aint)callstack_resolve_ref(state->callstack, ref);
          } break;

          case 3: { // C(m)
            uint32_t index = INT;
            DBG(" C(%d)", index);

            aint clos = callstack_closure(state->callstack);
            if (UNBOXED(clos) && UNBOX(clos) == 0) {
              failure(
                  "CLOSURE: capture C(%u) but no closure in current frame\n",
                  index);
            }

            // берём ref на captured cell из текущего closure
            args[i + 1] = closure_capture_ref(clos, index);
          } break;

          default:
            FAIL;
          }
        }

        aint clos_obj = (aint)Bclosure(args, BOX(n));
        callstack_push_operand(state->callstack, clos_obj);
      } break;

      case 5: /* CALLC */
      {
        int n = INT;
        uint32_t ret_off = (uint32_t)(state->ip - base_ip);

        // stack: ... [closure][arg0]...[arg(n-1)] (top = arg(n-1))
        aint *tmp = alloca(sizeof(aint) * (size_t)n);

        for (int i = n - 1; i >= 0; --i) {
          tmp[i] = callstack_pop_operand(state->callstack);
        }

        aint clos = callstack_pop_operand(state->callstack);

        for (int i = 0; i < n; ++i) {
          callstack_push_operand(state->callstack, tmp[i]);
        }

        void *entry = closure_entry_ptr(clos);

        callstack_push_cframe(state->callstack, clos, ret_off, (uint32_t)n);
        state->ip = (char *)entry;
      } break;

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

        aint p = callstack_pop_operand(state->callstack);

        aint th = LtagHash(tag);
        aint an = BOX(arity);

        aint r;

        if (arity == 0 && UNBOXED(p)) {
          /* immediate constructor  */
          r = (UNBOX(p) == UNBOX(th)) ? BOX(1) : BOX(0);
        } else {
          /* sexp / array / other */
          r = Btag((void *)p, th, an);
        }

        callstack_push_operand(state->callstack, r);
      } break;

      case 8: /* ARRAY */
      {
        int size = INT;

        aint p = callstack_pop_operand(state->callstack);

        aint r = Barray_patt((void *)p, BOX(size));

        callstack_push_operand(state->callstack, r);
      } break;

      case 9: /* FAIL */
      {
        int line = INT;
        int col = INT;
        char mainf[] = "main";

        aint p = callstack_pop_operand(state->callstack);

        Bmatch_failure((void *)p, mainf, BOX(line), BOX(col));
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
      switch (l) {
      case 0: /* PATT =str */
      {
        aint p1 = callstack_pop_operand(state->callstack);
        aint p2 = callstack_pop_operand(state->callstack);

        aint r = Bstring_patt((void *)p1, (void *)p2);

        callstack_push_operand(state->callstack, r);
      } break;

      case 1: /* PATT #string */
      {
        aint p = callstack_pop_operand(state->callstack);

        aint r = Bstring_tag_patt((void *)p);

        callstack_push_operand(state->callstack, r);
      } break;

      case 2: /* PATT #array */
      {
        aint p = callstack_pop_operand(state->callstack);

        aint r = Barray_tag_patt((void *)p);

        callstack_push_operand(state->callstack, r);
      } break;

      case 3: /* PATT #sexp */
      {
        aint p = callstack_pop_operand(state->callstack);

        aint r = Bsexp_tag_patt((void *)p);

        callstack_push_operand(state->callstack, r);
      } break;

      case 4: /* PATT #ref */
      {
        aint p = callstack_pop_operand(state->callstack);

        // Ref is boxed
        if (!UNBOXED(p))
          callstack_push_operand(state->callstack, BOX(1));

        // Fallback
        callstack_push_operand(state->callstack, BOX(0));
      } break;

      case 5: /* PATT #val */
      {
        aint p = callstack_pop_operand(state->callstack);

        // imm is unboxed
        if (UNBOXED(p))
          callstack_push_operand(state->callstack, BOX(1));

        // Fallback
        callstack_push_operand(state->callstack, BOX(0));
      } break;

      case 6: /* PATT #fun */
      {
        aint p = callstack_pop_operand(state->callstack);

        aint r = Bclosure_tag_patt((void *)p);

        // Fallback
        callstack_push_operand(state->callstack, r);
      } break;

      default:
        FAIL;
      }

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

        aint *elems = callstack_n_last_operands_sequence(state->callstack, n);
        void *a = Barray(elems, BOX(n));
        callstack_pop_n_operands(state->callstack, n);
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