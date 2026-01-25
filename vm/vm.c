#include <stdint.h>
#include <stdio.h>

#include "../runtime/gc.h"
#include "../runtime/runtime.h"

#include "bytecode.h"
#include "state.h"
#include "vm.h"

#ifdef DEBUG
#define DBG(...) fprintf(f, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

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

extern void *Bstring(aint *args);
extern aint LtagHash(char *);
extern void *Bsexp(aint *args, aint bn);
extern void *Bsta(void *x, aint i, void *v);
extern void *Belem(void *p, aint i);
extern void *Barray(aint *args, aint bn);
extern void *Bclosure(aint *args, aint bn);
extern aint Btag(void *d, aint t, aint n);
extern aint Barray_patt(void *d, aint n);
extern void Bmatch_failure(void *v, char *fname, aint line, aint col);

extern aint Bstring_patt(void *x, void *y);
extern aint Bstring_tag_patt(void *x);
extern aint Barray_tag_patt(void *x);
extern aint Bsexp_tag_patt(void *x);
extern aint Bclosure_tag_patt(void *x);

// Prevent dangling closure elem
static inline aint *alloc_capture_cell(aint v) {
  aint args[1] = {v};
  return (aint *)Barray(args, BOX(1));
}

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

static char current_h = 0;

typedef error_code_e (*op_handler)(FILE *f, struct interpreter_state_t *state,
                                   char l);

static const char *ops[] = {
    "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "!!"};
static const char *pats[] = {"=str", "#string", "#array", "#sexp",
                             "#ref", "#val",    "#fun"};
static const char *lds[] = {"LD", "LDA", "ST"};

static inline csval_t csval_from_aint(aint v) {
  return UNBOXED(v) ? csval_imm(v) : csval_extern(v);
}

static inline error_code_e csval_to_aint_checked(csval_t v, aint *out) {
  if (v.ty == CS_INTERNAL_REF) {
    return ERROR_NOT_BOXED;
  }
  *out = v.val;
  return ERROR_NONE;
}

static inline error_code_e csval_to_imm_checked(csval_t v, aint *out) {
  if (v.ty != CS_IMM || !UNBOXED(v.val)) {
    return ERROR_NOT_UNBOXED;
  }
  *out = v.val;
  return ERROR_NONE;
}

static inline csval_t csval_from_slot_words(const aint *slot_words) {
  aint type_word = slot_words[0];
  assert(UNBOXED(type_word));
  return (csval_t){.ty = (csval_type_e)UNBOX(type_word), .val = slot_words[1]};
}

static inline error_code_e store_internal_ref(struct callstack_t *stack,
                                              csval_t ref, csval_t value) {
  aint *slot_words = NULL;
  RETURN_IF_ERROR(csval_to_ref(stack, ref, &slot_words));
  slot_words[0] = BOX((aint)value.ty);
  slot_words[1] = value.val;
  return ERROR_NONE;
}

static inline error_code_e store_ref(struct callstack_t *stack, csval_t ref,
                                     csval_t value) {
  if (ref.ty == CS_INTERNAL_REF) {
    return store_internal_ref(stack, ref, value);
  }
  aint *refp = NULL;
  RETURN_IF_ERROR(csval_to_ref(stack, ref, &refp));
  aint v;
  RETURN_IF_ERROR(csval_to_aint_checked(value, &v));
  Bsta(refp, (aint)refp, (void *)v);
  return ERROR_NONE;
}

static error_code_e op_invalid(FILE *f, struct interpreter_state_t *state,
                               char l) {
  failure("ERROR: invalid opcode %d-%d\n", current_h, l);
  return ERROR_NONE;
}

static error_code_e op_stop(FILE *f, struct interpreter_state_t *state,
                            char l) {
  return ERROR_STOP;
}

static error_code_e op_binop(FILE *f, struct interpreter_state_t *state,
                             char l) {
  if (l == 0 || l > (char)(sizeof(ops) / sizeof(ops[0]))) {
    return op_invalid(f, state, l);
  }

  const char *op_name = ops[l - 1];
  DBG("BINOP\t%s", op_name);

  csval_t a_val, b_val;
  aint a, b;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &b_val));
  RETURN_IF_ERROR(csval_to_aint_checked(b_val, &b));
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &a_val));
  RETURN_IF_ERROR(csval_to_aint_checked(a_val, &a));
  aint result = 0;

  switch (l) {
  case 1:
    result = Ls__Infix_43((void *)a, (void *)b);
    break; /* + */
  case 2:
    result = Ls__Infix_45((void *)a, (void *)b);
    break; /* - */
  case 3:
    result = Ls__Infix_42((void *)a, (void *)b);
    break; /* * */
  case 4:
    result = Ls__Infix_47((void *)a, (void *)b);
    break; /* / */
  case 5:
    result = Ls__Infix_37((void *)a, (void *)b);
    break; /* % */
  case 6:
    result = Ls__Infix_60((void *)a, (void *)b);
    break; /* < */
  case 7:
    result = Ls__Infix_6061((void *)a, (void *)b);
    break; /* <= */
  case 8:
    result = Ls__Infix_62((void *)a, (void *)b);
    break; /* > */
  case 9:
    result = Ls__Infix_6261((void *)a, (void *)b);
    break; /* >= */
  case 10:
    result = Ls__Infix_6161((void *)a, (void *)b);
    break; /* == */
  case 11:
    result = Ls__Infix_3361((void *)a, (void *)b);
    break; /* != */
  case 12:
    result = Ls__Infix_3838((void *)a, (void *)b);
    break; /* && */
  case 13:
    result = Ls__Infix_3333((void *)a, (void *)b);
    break; /* !! */
  default:
    return op_invalid(f, state, l);
  }

  DBG("%" PRIdAI " %s %" PRIdAI " = %" PRIdAI, UNBOX(a), op_name, UNBOX(b),
      UNBOX(result));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_from_aint(result)));
  return ERROR_NONE;
}

static error_code_e op_const(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int32_t value = state_read_int(state);
  DBG("CONST\t%d", value);
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_imm(BOX(value))));
  return ERROR_NONE;
}

static error_code_e op_string(FILE *f, struct interpreter_state_t *state,
                              char l) {
  char *str = state_read_string(state);
  DBG("STRING (%s)\n", str);
  RETURN_IF_ERROR(callstack_push_operand(
      state_cs(state), csval_extern((aint)Bstring((aint *)&str))));
  return ERROR_NONE;
}

static error_code_e op_sexp(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t args_ref;
  char *tag = state_read_string(state);
  int32_t arity = state_read_int(state);
  DBG("SEXP\t%s %d", tag, arity);
  aint th = LtagHash(tag);
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(th)));
  RETURN_IF_ERROR(
      callstack_pop_n_operands(state_cs(state), arity + 1, &args_ref));
  aint *slot_words = NULL;
  RETURN_IF_ERROR(csval_to_ref(state_cs(state), args_ref, &slot_words));
  uint32_t nargs = (uint32_t)(arity + 1);
  aint args[nargs];
  for (uint32_t i = 0; i < nargs; i++) {
    csval_t v = csval_from_slot_words(slot_words + (i * CSVAL_WORDS));
    RETURN_IF_ERROR(csval_to_aint_checked(v, &args[i]));
  }
  void *r = Bsexp(args, BOX(nargs /* With tag*/));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_extern((aint)r)));
  return ERROR_NONE;
}

static error_code_e op_sti(FILE *f, struct interpreter_state_t *state, char l) {
  csval_t val;
  csval_t ref;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &ref));
  DBG("STI");
  RETURN_IF_ERROR(store_ref(state_cs(state), ref, val));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), val));
  return ERROR_NONE;
}

static error_code_e op_sta(FILE *f, struct interpreter_state_t *state, char l) {
  csval_t val;
  csval_t sec_op;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &sec_op));

  // case AGGREGATE: agg idx val
  if (sec_op.ty == CS_IMM) {
    csval_t agg;
    aint agg_val;
    aint idx_val;
    aint val_aint;
    RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &agg));
    RETURN_IF_ERROR(csval_to_aint_checked(agg, &agg_val));
    RETURN_IF_ERROR(csval_to_imm_checked(sec_op, &idx_val));
    RETURN_IF_ERROR(csval_to_aint_checked(val, &val_aint));
    DBG("STA\t%d %" PRIdAI, UNBOX(idx_val), UNBOX(val_aint));
    Bsta((void *)agg_val, idx_val, (void *)val_aint);
  }
  // case REF: ref val
  else {
    DBG("STA");
    RETURN_IF_ERROR(store_ref(state_cs(state), sec_op, val));
  }
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), val));
  return ERROR_NONE;
}

static error_code_e op_jmp(FILE *f, struct interpreter_state_t *state, char l) {
  int offset = state_read_int(state);
  DBG("JMP\t0x%.8x", offset);
  RETURN_IF_ERROR(state_jmp(state, offset));
  return ERROR_NONE;
}

static error_code_e op_end(FILE *f, struct interpreter_state_t *state, char l) {
  csval_t callee_ret;
  uint32_t ret_off;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &callee_ret));
  RETURN_IF_ERROR(callstack_pop_frame(state_cs(state), &ret_off));
  if (callstack_nframes(state_cs(state)) == 0) {
    return ERROR_STOP;
  }
  DBG("END\t%p", ret_off);
  RETURN_IF_ERROR(callstack_push_operand(
      state_cs(state), callee_ret)); // Put retval on caller stack
  RETURN_IF_ERROR(state_jmp(state, ret_off));
  return ERROR_NONE;
}

static error_code_e op_ret(FILE *f, struct interpreter_state_t *state, char l) {
  csval_t callee_ret;
  uint32_t ret_off;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &callee_ret));
  RETURN_IF_ERROR(callstack_pop_frame(state_cs(state), &ret_off));
  if (callstack_nframes(state_cs(state)) == 0) {
    return ERROR_STOP;
  }
  DBG("RET\t%p", ret_off);
  RETURN_IF_ERROR(callstack_push_operand(
      state_cs(state), callee_ret)); // Put retval on caller stack
  RETURN_IF_ERROR(state_jmp(state, ret_off));
  return ERROR_NONE;
}

static error_code_e op_drop(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t val;
  DBG("DROP");
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  return ERROR_NONE;
}

static error_code_e op_dup(FILE *f, struct interpreter_state_t *state, char l) {
  csval_t value;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &value));
  DBG("DUP");
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), value));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), value));
  return ERROR_NONE;
}

static error_code_e op_swap(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t a;
  csval_t b;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &a));
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &b));
  DBG("SWAP");
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), a));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), b));
  return ERROR_NONE;
}

static error_code_e op_elem(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t idx;
  csval_t agg;
  aint idx_val;
  aint agg_val;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &idx));
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &agg));
  RETURN_IF_ERROR(csval_to_imm_checked(idx, &idx_val));
  RETURN_IF_ERROR(csval_to_aint_checked(agg, &agg_val));
  DBG("ELEM\t%d %" PRIdAI, UNBOX(idx_val), UNBOX(agg_val));
  void *r = Belem((void *)agg_val, idx_val);
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_from_aint((aint)r)));
  return ERROR_NONE;
}

static error_code_e op_ld_g(FILE *f, struct interpreter_state_t *state,
                            char l) {
  aint val;
  int index = state_read_int(state);
  DBG("LD\tG(%d)", index);
  RETURN_IF_ERROR(state_get_glob(state, index, &val));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_from_aint(val)));
  return ERROR_NONE;
}

static error_code_e op_ld_l(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t val;
  int index = state_read_int(state);
  DBG("LD\tL(%d)", index);
  RETURN_IF_ERROR(callstack_get_local(state_cs(state), index, &val));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), val));
  return ERROR_NONE;
}

static error_code_e op_ld_a(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t val;
  int index = state_read_int(state);
  DBG("LD\tA(%d)", index);
  RETURN_IF_ERROR(callstack_get_arg(state_cs(state), index, &val));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), val));
  return ERROR_NONE;
}

static error_code_e op_ld_c(FILE *f, struct interpreter_state_t *state,
                            char l) {
  int32_t index = state_read_int(state);
  aint clos = callstack_closure(state_cs(state));
  if (UNBOXED(clos) && UNBOX(clos) == 0) {
    DBG("LD\tC(%d): no closure in current frame\n", index);
    return ERROR_NO_CLOSURE_IN_CURRENT_FRAME;
  }

  DBG("LD\tC(%d)", index);
  aint ref = closure_capture_ref(clos, (uint32_t)index);
  aint *cell = NULL;
  RETURN_IF_ERROR(csval_to_ref(state_cs(state), csval_extern(ref), &cell));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_from_aint(*cell)));
  return ERROR_NONE;
}

static error_code_e op_lda_g(FILE *f, struct interpreter_state_t *state,
                             char l) {
  aint *ref;
  int32_t index = state_read_int(state);
  DBG("LDA\tG(%d)", index);
  RETURN_IF_ERROR(state_get_glob_addr(state, index, &ref));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_extern((aint)ref)));
  return ERROR_NONE;
}

static error_code_e op_lda_l(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int32_t index = state_read_int(state);

  DBG("LDA\tL(%d)", index);
  csval_t ref;
  RETURN_IF_ERROR(callstack_get_local_addr(state_cs(state), index, &ref));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), ref));
  return ERROR_NONE;
}

static error_code_e op_lda_a(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int32_t index = state_read_int(state);
  DBG("LDA\tA(%d)", index);
  csval_t ref;
  RETURN_IF_ERROR(callstack_get_arg_addr(state_cs(state), index, &ref));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), ref));
  return ERROR_NONE;
}

static error_code_e op_lda_c(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int32_t index = state_read_int(state);
  aint clos = callstack_closure(state_cs(state));
  if (UNBOXED(clos) && UNBOX(clos) == 0) {
    DBG("LDA\tC(%d): no closure in current frame\n", index);
    return ERROR_NO_CLOSURE_IN_CURRENT_FRAME;
  }

  DBG("LDA\tC(%d)", index);
  aint ref = closure_capture_ref(clos, (uint32_t)index);
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state),
                                         csval_extern(ref))); // direct ref
  return ERROR_NONE;
}

static error_code_e op_st_g(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t val;
  int index = state_read_int(state);
  DBG("ST\tG(%d)", index);
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  aint val_aint;
  RETURN_IF_ERROR(csval_to_aint_checked(val, &val_aint));
  RETURN_IF_ERROR(state_set_glob(state, index, val_aint));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), val)); /* Push back onto stack */
  return ERROR_NONE;
}

static error_code_e op_st_l(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t val;
  int index = state_read_int(state);
  DBG("ST\tL(%d)", index);
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  RETURN_IF_ERROR(callstack_set_local(state_cs(state), index, val));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), val)); /* Push back onto stack */
  return ERROR_NONE;
}

static error_code_e op_st_a(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t val;
  int index = state_read_int(state);
  DBG("ST\tA(%d)", index);
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  RETURN_IF_ERROR(callstack_set_arg(state_cs(state), index, val));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), val)); /* Push back onto stack */
  return ERROR_NONE;
}

static error_code_e op_st_c(FILE *f, struct interpreter_state_t *state,
                            char l) {
  int32_t index = state_read_int(state);
  csval_t val;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  aint a;
  RETURN_IF_ERROR(csval_to_aint_checked(val, &a));

  aint clos = callstack_closure(state_cs(state));
  if (UNBOXED(clos) && UNBOX(clos) == 0) {
    DBG("ST\tC(%d): no closure in current frame\n", index);
    return ERROR_NO_CLOSURE_IN_CURRENT_FRAME;
  }

  DBG("ST\tC(%d)", index);
  aint ref = closure_capture_ref(clos, (uint32_t)index);
  aint *cell = NULL;
  RETURN_IF_ERROR(csval_to_ref(state_cs(state), csval_extern(ref), &cell));
  *cell = a;

  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), val));
  return ERROR_NONE;
}

static error_code_e op_cjmpz(FILE *f, struct interpreter_state_t *state,
                             char l) {
  csval_t val;
  aint cond;
  int32_t l_offset = state_read_int(state);
  DBG("CJMPz\t0x%.8lx", l_offset);
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  RETURN_IF_ERROR(csval_to_imm(state_cs(state), val, &cond));
  if (!UNBOX(cond))
    RETURN_IF_ERROR(state_jmp(state, l_offset));
  return ERROR_NONE;
}

static error_code_e op_cjmpnz(FILE *f, struct interpreter_state_t *state,
                              char l) {
  csval_t val;
  aint cond;
  int32_t l_offset = state_read_int(state);
  DBG("CJMPnz\t0x%.8lx", l_offset);
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &val));
  RETURN_IF_ERROR(csval_to_imm(state_cs(state), val, &cond));
  if (UNBOX(cond))
    RETURN_IF_ERROR(state_jmp(state, l_offset));
  return ERROR_NONE;
}

static error_code_e op_begin(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int nargs = state_read_int(state);
  int nlocals = state_read_int(state);
  DBG("BEGIN\t%d %d", nargs, nlocals);
  RETURN_IF_ERROR(callstack_alloc_locals(state_cs(state), nlocals));
  return ERROR_NONE;
}

static error_code_e op_cbegin(FILE *f, struct interpreter_state_t *state,
                              char l) {
  int value = state_read_int(state);
  int offset = state_read_int(state);
  DBG("CBEGIN\t%d ", value);
  DBG("%d", offset);
  return ERROR_NONE;
}

static error_code_e op_closure(FILE *f, struct interpreter_state_t *state,
                               char l) {
  int32_t l_offset = state_read_int(state);
  DBG("CLOSURE\t0x%.8x", l_offset);

  int n = state_read_int(state);

  // args[0] = entry pointer, args[1..n] = captured refs
  aint *args = alloca(sizeof(aint) * (n + 1));
  args[0] = (aint)(state_base_ip(state) + l_offset);

  for (int i = 0; i < n; i++) {
    switch (state_read_byte(state)) {
    case 0: { // G(m)
      uint32_t index = state_read_int(state);
      DBG(" G(%d)", index);
      aint *cell = NULL;
      RETURN_IF_ERROR(state_get_glob_addr(state, index, &cell));
      args[i + 1] = (aint)cell;
    } break;

    case 1: { // L(m)
      uint32_t index = state_read_int(state);
      DBG(" L(%d)", index);
      /* Capture local by stable heap cell (value at closure creation
       * time). */
      csval_t v_val;
      aint v;
      RETURN_IF_ERROR(callstack_get_local(state_cs(state), index, &v_val));
      RETURN_IF_ERROR(csval_to_aint_checked(v_val, &v));
      aint *cell = alloc_capture_cell(v);
      args[i + 1] = (aint)cell; /* direct-address ref */
    } break;

    case 2: { // A(m)
      uint32_t index = (uint32_t)state_read_int(state);
      DBG(" A(%d)", index);
      /* Capture argument by stable heap cell (value at closure creation
       * time). */
      csval_t v_val;
      aint v;
      RETURN_IF_ERROR(callstack_get_arg(state_cs(state), index, &v_val));
      RETURN_IF_ERROR(csval_to_aint_checked(v_val, &v));
      aint *cell = alloc_capture_cell(v);
      args[i + 1] = (aint)cell; /* direct-address ref */
    } break;

    case 3: { // C(m)
      uint32_t index = (uint32_t)state_read_int(state);
      DBG(" C(%d)", index);

      aint clos = callstack_closure(state_cs(state));
      if (UNBOXED(clos) && UNBOX(clos) == 0) {
        failure("CLOSURE: capture C(%u) but no closure in current frame\n",
                index);
      }

      // берём ref на captured cell из текущего closure
      args[i + 1] = closure_capture_ref(clos, index);
    } break;

    default:
      DBG("CLOSURE: invalid capture type %d\n", kind);
      return ERROR_INVALID_CAPTURE_TYPE;
    }
  }

  aint clos_obj = (aint)Bclosure(args, BOX(n));
  RETURN_IF_ERROR(
      callstack_push_operand(state_cs(state), csval_extern(clos_obj)));
  return ERROR_NONE;
}

static error_code_e op_callc(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int arity = state_read_int(state);
  DBG("CALLC\t%d", arity);
  return ERROR_NONE;
}

static error_code_e op_call(FILE *f, struct interpreter_state_t *state,
                            char l) {
  int offset = state_read_int(state);
  int nargs = state_read_int(state);
  DBG("CALL\t0x%.8x %d", offset, nargs);
  RETURN_IF_ERROR(
      callstack_push_frame(state_cs(state), state_ip_off(state), nargs));
  RETURN_IF_ERROR(state_jmp(state, offset));
  return ERROR_NONE;
}

static error_code_e op_tag(FILE *f, struct interpreter_state_t *state, char l) {
  char *tag = state_read_string(state);
  int arity = state_read_int(state);

  aint r;
  csval_t p_val;
  aint p;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &p_val));
  RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
  aint th = LtagHash(tag);
  aint an = BOX(arity);
  DBG("TAG\t%s %d", tag, arity);
  if (arity == 0 && UNBOXED(p)) {
    /* immediate constructor  */
    r = (UNBOX(p) == UNBOX(th)) ? BOX(1) : BOX(0);
  } else {
    /* sexp / array / other */
    r = Btag((void *)p, th, an);
  }
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_from_aint(r)));
  return ERROR_NONE;
}

static error_code_e op_array(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int size = state_read_int(state);
  csval_t p_val;
  aint p;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &p_val));
  RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
  DBG("ARRAY\t%d %" PRIdAI, size, UNBOX(p));
  aint r = Barray_patt((void *)p, BOX(size));
  RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_from_aint(r)));
  return ERROR_NONE;
}

static error_code_e op_fail(FILE *f, struct interpreter_state_t *state,
                            char l) {
  int line = state_read_int(state);
  int col = state_read_int(state);
  char mainf[] = "main";

  csval_t p_val;
  aint p;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &p_val));
  RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
  DBG("FAIL\t%d %d", line, col);
  Bmatch_failure((void *)p, mainf, BOX(line), BOX(col));
  return ERROR_NONE;
}

static error_code_e op_line(FILE *f, struct interpreter_state_t *state,
                            char l) {
  int line = state_read_int(state);
  DBG("LINE\t%d", line);
  return ERROR_NONE;
}

static error_code_e op_patt(FILE *f, struct interpreter_state_t *state,
                            char l) {
  csval_t p_val;
  RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &p_val));

  switch (l) {
  case 0: { /* PATT =str */
    aint p;
    RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
    csval_t p2_val;
    aint p2;
    RETURN_IF_ERROR(callstack_pop_operand(state_cs(state), &p2_val));
    RETURN_IF_ERROR(csval_to_aint_checked(p2_val, &p2));

    DBG("PATT\t=str\t%d %d", UNBOX(p), UNBOX(p2));
    aint r = Bstring_patt((void *)p, (void *)p2);
    RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(r)));
  } break;

  case 1: { /* PATT #string */
    aint p;
    RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
    aint r = Bstring_tag_patt((void *)p);
    RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(r)));
  } break;

  case 2: { /* PATT #array */
    aint p;
    RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
    DBG("PATT\t#array\t%d", UNBOX(p));
    aint r = Barray_tag_patt((void *)p);
    RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(r)));
  } break;

  case 3: { /* PATT #sexp */
    aint p;
    RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
    DBG("PATT\t#sexp\t%d", UNBOX(p));
    aint r = Bsexp_tag_patt((void *)p);
    RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(r)));
  } break;

  case 4: { /* PATT #ref */
    aint r = (p_val.ty == CS_INTERNAL_REF) ? BOX(1) : BOX(0);
    RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(r)));
  } break;

  case 5: { /* PATT #val */
    aint r = (p_val.ty == CS_IMM) ? BOX(1) : BOX(0);
    RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(r)));
  } break;

  case 6: { /* PATT #fun */
    aint p;
    RETURN_IF_ERROR(csval_to_aint_checked(p_val, &p));
    DBG("PATT\t#fun\t%d", UNBOX(p));
    aint r = Bclosure_tag_patt((void *)p);
    RETURN_IF_ERROR(callstack_push_operand(state_cs(state), csval_imm(r)));
  } break;

  default:
    return op_invalid(f, state, l);
  }
  return ERROR_NONE;
}

static error_code_e op_call_lread(FILE *f, struct interpreter_state_t *state,
                                  char l) {
  DBG("CALL\tLread");
  return ERROR_NONE;
}

static error_code_e op_call_lwrite(FILE *f, struct interpreter_state_t *state,
                                   char l) {
  DBG("CALL\tLwrite");
  return ERROR_NONE;
}

static error_code_e op_call_llength(FILE *f, struct interpreter_state_t *state,
                                    char l) {
  DBG("CALL\tLlength");
  return ERROR_NONE;
}

static error_code_e op_call_lstring(FILE *f, struct interpreter_state_t *state,
                                    char l) {
  DBG("CALL\tLstring");
  return ERROR_NONE;
}

static error_code_e op_call_barray(FILE *f, struct interpreter_state_t *state,
                                   char l) {
  int size = state_read_int(state);
  DBG("CALL\tBarray\t%d", size);
  return ERROR_NONE;
}

static void init_handlers(op_handler handlers[16][16]) {
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 16; j++) {
      handlers[i][j] = op_invalid;
    }
  }

  for (int j = 0; j < 16; j++) {
    handlers[15][j] = op_stop;
  }

  for (int j = 0; j < (int)(sizeof(ops) / sizeof(ops[0])); j++) {
    handlers[0][j] = &op_binop;
  }

  handlers[1][0] = &op_const;
  handlers[1][1] = &op_string;
  handlers[1][2] = &op_sexp;
  handlers[1][3] = &op_sti;
  handlers[1][4] = &op_sta;
  handlers[1][5] = &op_jmp;
  handlers[1][6] = &op_end;
  handlers[1][7] = &op_ret;
  handlers[1][8] = &op_drop;
  handlers[1][9] = &op_dup;
  handlers[1][10] = &op_swap;
  handlers[1][11] = &op_elem;

  handlers[2][0] = &op_ld_g;
  handlers[2][1] = &op_ld_l;
  handlers[2][2] = &op_ld_a;
  handlers[2][3] = &op_ld_c;
  handlers[3][0] = &op_lda_g;
  handlers[3][1] = &op_lda_l;
  handlers[3][2] = &op_lda_a;
  handlers[3][3] = &op_lda_c;
  handlers[4][0] = &op_st_g;
  handlers[4][1] = &op_st_l;
  handlers[4][2] = &op_st_a;
  handlers[4][3] = &op_st_c;

  handlers[5][0] = &op_cjmpz;
  handlers[5][1] = &op_cjmpnz;
  handlers[5][2] = &op_begin;
  handlers[5][3] = &op_cbegin;
  handlers[5][4] = &op_closure;
  handlers[5][5] = &op_callc;
  handlers[5][6] = &op_call;
  handlers[5][7] = &op_tag;
  handlers[5][8] = &op_array;
  handlers[5][9] = &op_fail;
  handlers[5][10] = &op_line;

  for (int j = 0; j < 16; j++) {
    handlers[6][j] = &op_patt;
  }

  handlers[7][0] = &op_call_lread;
  handlers[7][1] = &op_call_lwrite;
  handlers[7][2] = &op_call_llength;
  handlers[7][3] = &op_call_lstring;
  handlers[7][4] = &op_call_barray;
}

void interpret_bc(FILE *f, struct interpreter_state_t *state,
                  error_code_e *error_code) {
  char *base_ip = state_ip(state);
  static op_handler handlers[16][16];
  init_handlers(handlers);

  for (;;) {
    char x = (char)state_read_byte(state);
    char h = (x & 0xF0) >> 4;
    char l = x & 0x0F;

    DBG("0x%.8lx:\t", state_ip(state) - base_ip - 1);
    current_h = h;
    if ((*error_code = handlers[h][l](f, state, l)) != ERROR_NONE) {
      break;
    }
    DBG("\n");
  }
}