#include <stdint.h>
#include <stdio.h>

#include "../runtime/gc.h"
#include "../runtime/runtime.h"

#include "bytefile.h"
#include "state.h"
#include "verify_run.h"

/* High nibble (h) */
enum op_high {
  OP_H_BINOP = 0,
  OP_H_OP1 = 1,
  OP_H_LD = 2,
  OP_H_LDA = 3,
  OP_H_ST = 4,
  OP_H_OP5 = 5,
  OP_H_PATT = 6,
  OP_H_BUILTIN = 7,
  OP_H_STOP = 15
};

/* Low nibble for OP_H_BINOP (h=0) */
enum op_l_binop {
  OP_L_BINOP_ADD = 1,
  OP_L_BINOP_SUB = 2,
  OP_L_BINOP_MUL = 3,
  OP_L_BINOP_DIV = 4,
  OP_L_BINOP_MOD = 5,
  OP_L_BINOP_LT = 6,
  OP_L_BINOP_LE = 7,
  OP_L_BINOP_GT = 8,
  OP_L_BINOP_GE = 9,
  OP_L_BINOP_EQ = 10,
  OP_L_BINOP_NE = 11,
  OP_L_BINOP_AND = 12,
  OP_L_BINOP_OR = 13
};

/* Low nibble for OP_H_OP1 (h=1) */
enum op_l_op1 {
  OP_L_CONST = 0,
  OP_L_STRING = 1,
  OP_L_SEXP = 2,
  OP_L_STI = 3,
  OP_L_STA = 4,
  OP_L_JMP = 5,
  OP_L_END = 6,
  OP_L_RET = 7,
  OP_L_DROP = 8,
  OP_L_DUP = 9,
  OP_L_SWAP = 10,
  OP_L_ELEM = 11
};

/* Low nibble for LD/LDA/ST (h=2,3,4) */
enum op_l_ld_st { OP_L_G = 0, OP_L_L = 1, OP_L_A = 2, OP_L_C = 3 };

/* Low nibble for OP_H_OP5 (h=5) */
enum op_l_op5 {
  OP_L_CJMPz = 0,
  OP_L_CJMPnz = 1,
  OP_L_BEGIN = 2,
  OP_L_CBEGIN = 3,
  OP_L_CLOSURE = 4,
  OP_L_CALLC = 5,
  OP_L_CALL = 6,
  OP_L_TAG = 7,
  OP_L_ARRAY = 8,
  OP_L_FAIL = 9,
  OP_L_LINE = 10
};

/* Low nibble for OP_H_BUILTIN (h=7) */
enum op_l_builtin {
  OP_L_LREAD = 0,
  OP_L_LWRITE = 1,
  OP_L_LLENGTH = 2,
  OP_L_LSTRING = 3,
  OP_L_BARRAY = 4
};

/* Low nibble for OP_H_PATT (h=6) */
enum op_l_patt {
  OP_L_PATT_STR = 0,        /* =str */
  OP_L_PATT_STRING_TAG = 1, /* #string */
  OP_L_PATT_ARRAY_TAG = 2,  /* #array */
  OP_L_PATT_SEXP_TAG = 3,   /* #sexp */
  OP_L_PATT_REF = 4,        /* #ref */
  OP_L_PATT_VAL = 5,        /* #val */
  OP_L_PATT_FUN = 6         /* #fun */
};

/* Virtual regs */
char *__ip = NULL;  /* address of current instruction */
size_t __cs_fp = 0; /* slot index of nlocals for current frame */
/* Internal */
size_t __cs_cap = 0;
size_t __cs_nframes = 0;
size_t __cs_nglob = 0;
bytefile *__bf = NULL;

#ifdef DEBUG
#define DBG(...) fprintf(f, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

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

extern aint Lread();
extern aint Lwrite(aint n);
extern aint Llength(void *p);
extern void *Lstring(aint *args);

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

static inline aint *closure_capture_slot_addr(aint clos, uint32_t idx) {
  if (UNBOXED(clos)) {
    failure("closure_capture_slot_addr: closure expected, got unboxed\n");
  }
  data *d = TO_DATA((void *)clos);
  if (TAG(d->data_header) != CLOSURE_TAG) {
    failure("closure_capture_slot_addr: closure expected, tag=%ld\n",
            TAG(d->data_header));
  }
  aint len = LEN(d->data_header);
  if ((aint)(idx + 1) >= len) {
    failure("closure_capture_slot_addr: index %u out of range (len=%ld)\n", idx,
            len);
  }
  return ((aint *)d->contents) + (idx + 1);
}

static char current_h = 0;

static const char *const ops[] = {
    "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "!!"};
static const char *const pats[] = {"=str", "#string", "#array", "#sexp",
                                   "#ref", "#val",    "#fun"};
static const char *const lds[] = {"LD", "LDA", "ST"};

static inline csval_t csval_from_slot_words(const aint *slot_words) {
  aint type_word = slot_words[0];
  assert(UNBOXED(type_word));
  return (csval_t){.ty = (csval_type_e)UNBOX(type_word), .val = slot_words[1]};
}

static inline error_code_e store_by_internal_ref(csval_t ref, csval_t value) {
  aint *slot_words = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(ref, &slot_words));
  slot_words[0] = BOX((aint)value.ty);
  slot_words[1] = value.val;
  return ERROR_NONE;
}

static inline error_code_e store_by_ref(csval_t ref, csval_t value) {
  if (ref.ty == CS_INTERNAL_REF) {
    return store_by_internal_ref(ref, value);
  }
  aint *refp = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(ref, &refp));
  aint v;
  RETURN_IF_ERROR(csval_to_imm_aint(value, &v));
  Bsta(refp, (aint)refp, (void *)v);
  return ERROR_NONE;
}

static error_code_e op_invalid(FILE *f, char l) {
  failure("ERROR: invalid opcode %d-%d\n", current_h, l);
  return ERROR_NONE;
}

static error_code_e op_stop(FILE *f, char l) { return ERROR_STOP; }

static error_code_e op_binop(FILE *f, char l) {
  if (l < OP_L_BINOP_ADD || l > OP_L_BINOP_OR) {
    return op_invalid(f, l);
  }

  const char *op_name = ops[l - 1];
  DBG("BINOP\t%s", op_name);

  csval_t a_val, b_val;
  RETURN_IF_ERROR(callstack_pop_operand(&b_val));
  RETURN_IF_ERROR(callstack_pop_operand(&a_val));
  aint a = UNBOX(csval_to_aint(a_val));
  aint b = UNBOX(csval_to_aint(b_val));
  aint result_unbox = 0;

  switch (l) {
  case OP_L_BINOP_ADD:
    result_unbox = a + b;
    break;
  case OP_L_BINOP_SUB:
    result_unbox = a - b;
    break;
  case OP_L_BINOP_MUL:
    result_unbox = a * b;
    break;
  case OP_L_BINOP_DIV:
    if (b == 0) {
      failure("integer division by zero at ip=0x%.8lx\n",
              (unsigned long)ip_offset());
    }
    result_unbox = a / b;
    break;
  case OP_L_BINOP_MOD:
    if (b == 0) {
      failure("integer modulo by zero at ip=0x%.8lx\n",
              (unsigned long)ip_offset());
    }
    result_unbox = a % b;
    break;
  case OP_L_BINOP_LT:
    result_unbox = a < b;
    break;
  case OP_L_BINOP_LE:
    result_unbox = a <= b;
    break;
  case OP_L_BINOP_GT:
    result_unbox = a > b;
    break;
  case OP_L_BINOP_GE:
    result_unbox = a >= b;
    break;
  case OP_L_BINOP_EQ:
    result_unbox = a == b;
    break;
  case OP_L_BINOP_NE:
    result_unbox = a != b;
    break;
  case OP_L_BINOP_AND:
    result_unbox = a && b;
    break;
  case OP_L_BINOP_OR:
    result_unbox = a || b;
    break;
  default:
    return op_invalid(f, l);
  }
  aint result = BOX(result_unbox);

  DBG("%" PRIdAI " %s %" PRIdAI " = %" PRIdAI, UNBOX(a), op_name, UNBOX(b),
      UNBOX(result));
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(result)));
  return ERROR_NONE;
}

static error_code_e op_const(FILE *f, char l) {
  int32_t value = bc_read_int();
  DBG("CONST\t%d", value);
  RETURN_IF_ERROR(callstack_push_operand(csval_imm(value)));
  return ERROR_NONE;
}

static error_code_e op_string(FILE *f, char l) {
  const char *str = bc_read_string();
  DBG("STRING (%s)\n", str);
  RETURN_IF_ERROR(
      callstack_push_operand(csval_extern((aint *)Bstring((aint *)&str))));
  return ERROR_NONE;
}

static error_code_e op_sexp(FILE *f, char l) {
  const char *tag = bc_read_string();
  int32_t arity = bc_read_int();
  DBG("SEXP\t%s %d", tag, arity);
  aint th = LtagHash((char *)tag);
  csval_t args_ref = callstack_operands_tail_ref((uint32_t)arity);
  aint *slot_words = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(args_ref, &slot_words));

  sexp *r = alloc_sexp(arity);
  for (uint32_t i = 0; i < (uint32_t)arity; i++) {
    csval_t v = csval_from_slot_words(slot_words + (i * CSVAL_WORDS));
    ((auint *)r->contents)[i] = csval_to_aint(v);
  }
  r->tag = UNBOX(th);

  RETURN_IF_ERROR(callstack_pop_n_operands(
      (uint32_t)arity)); // Shrink bottom n operands used before
  RETURN_IF_ERROR(
      callstack_push_operand(csval_extern((aint *)((data *)r)->contents)));
  return ERROR_NONE;
}

static error_code_e op_sti(FILE *f, char l) {
  csval_t val;
  csval_t ref;
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  RETURN_IF_ERROR(callstack_pop_operand(&ref));
  DBG("STI");
  RETURN_IF_ERROR(store_by_ref(ref, val));
  RETURN_IF_ERROR(callstack_push_operand(val));
  return ERROR_NONE;
}

static error_code_e op_sta(FILE *f, char l) {
  csval_t val;
  csval_t sec_op;
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  RETURN_IF_ERROR(callstack_pop_operand(&sec_op));

  // case AGGREGATE: agg idx val
  if (sec_op.ty == CS_IMM) {
    csval_t agg;
    aint *agg_val;
    aint idx_val;
    aint val_aint;
    RETURN_IF_ERROR(callstack_pop_operand(&agg));
    RETURN_IF_ERROR(csval_to_ref_aintp(agg, &agg_val));
    RETURN_IF_ERROR(csval_to_imm_aint(sec_op, &idx_val));
    RETURN_IF_ERROR(csval_to_imm_aint(val, &val_aint));
    DBG("STA\t%lld %" PRIdAI, UNBOX(idx_val), UNBOX(val_aint));
    Bsta(agg_val, idx_val, (void *)val_aint);
  }
  // case REF: ref val
  else {
    DBG("STA");
    RETURN_IF_ERROR(store_by_ref(sec_op, val));
  }
  RETURN_IF_ERROR(callstack_push_operand(val));
  return ERROR_NONE;
}

static error_code_e op_jmp(FILE *f, char l) {
  int offset = bc_read_int();
  DBG("JMP\t0x%.8x", offset);
  RETURN_IF_ERROR(ip_jmp(offset));
  return ERROR_NONE;
}

static error_code_e op_end(FILE *f, char l) {
  csval_t callee_ret;
  uint32_t ret_off;
  size_t nargs = cs_nargs();
  RETURN_IF_ERROR(callstack_pop_operand(&callee_ret));
  RETURN_IF_ERROR(cs_pop_frame(&ret_off));
  if (__cs_nframes == 0) {
    return ERROR_STOP;
  }
  DBG("END\t%u", ret_off);
  for (size_t i = 0; i < nargs; i++)
    RETURN_IF_ERROR(callstack_pop_operand(NULL));
  RETURN_IF_ERROR(
      callstack_push_operand(callee_ret)); // Put retval on caller stack
  RETURN_IF_ERROR(ip_jmp(ret_off));
  return ERROR_NONE;
}

static error_code_e op_drop(FILE *f, char l) {
  csval_t val;
  DBG("DROP");
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  return ERROR_NONE;
}

static error_code_e op_dup(FILE *f, char l) {
  csval_t value;
  RETURN_IF_ERROR(callstack_pop_operand(&value));
  DBG("DUP");
  RETURN_IF_ERROR(callstack_push_operand(value));
  RETURN_IF_ERROR(callstack_push_operand(value));
  return ERROR_NONE;
}

static error_code_e op_swap(FILE *f, char l) {
  csval_t a;
  csval_t b;
  RETURN_IF_ERROR(callstack_pop_operand(&a));
  RETURN_IF_ERROR(callstack_pop_operand(&b));
  DBG("SWAP");
  RETURN_IF_ERROR(callstack_push_operand(a));
  RETURN_IF_ERROR(callstack_push_operand(b));
  return ERROR_NONE;
}

static error_code_e op_elem(FILE *f, char l) {
  csval_t idx;
  csval_t agg;
  aint idx_val;
  aint agg_val;
  RETURN_IF_ERROR(callstack_pop_operand(&idx));
  RETURN_IF_ERROR(callstack_pop_operand(&agg));
  RETURN_IF_ERROR(csval_to_imm_aint(idx, &idx_val));
  agg_val = csval_to_aint(agg);
  DBG("ELEM\t%lld %" PRIdAI, UNBOX(idx_val), UNBOX(agg_val));
  void *r = Belem((void *)agg_val, idx_val);
  csval_t pushed = csval_from_aint((aint)r);
  RETURN_IF_ERROR(callstack_push_operand(pushed));
  return ERROR_NONE;
}

static error_code_e op_ld_g(FILE *f, char l) {
  csval_t val;
  int index = bc_read_int();
  DBG("LD\tG(%d)", index);
  RETURN_IF_ERROR(callstack_get_glob(index, &val));
  RETURN_IF_ERROR(callstack_push_operand(val));
  return ERROR_NONE;
}

static error_code_e op_ld_l(FILE *f, char l) {
  csval_t val;
  int index = bc_read_int();
  DBG("LD\tL(%d)", index);
  RETURN_IF_ERROR(callstack_get_local(index, &val));
  RETURN_IF_ERROR(callstack_push_operand(val));
  return ERROR_NONE;
}

static error_code_e op_ld_a(FILE *f, char l) {
  csval_t val;
  int index = bc_read_int();
  DBG("LD\tA(%d)", index);
  RETURN_IF_ERROR(callstack_get_arg(index, &val));
  RETURN_IF_ERROR(callstack_push_operand(val));
  return ERROR_NONE;
}

static error_code_e op_ld_c(FILE *f, char l) {
  int32_t index = bc_read_int();
  aint clos = cs_clos();
  if (UNBOXED(clos) && UNBOX(clos) == 0) {
    DBG("LD\tC(%d): no closure in current frame\n", index);
    return ERROR_NO_CLOSURE_IN_CURRENT_FRAME;
  }

  DBG("LD\tC(%d)", index);
  aint val = closure_capture_ref(clos, (uint32_t)index);
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(val)));
  return ERROR_NONE;
}

static error_code_e op_lda_g(FILE *f, char l) {
  int32_t index = bc_read_int();
  DBG("LDA\tG(%d)", index);
  csval_t ref;
  RETURN_IF_ERROR(callstack_get_glob_addr(index, &ref));
  RETURN_IF_ERROR(callstack_push_operand(ref));
  return ERROR_NONE;
}

static error_code_e op_lda_l(FILE *f, char l) {
  int32_t index = bc_read_int();
  DBG("LDA\tL(%d)", index);
  csval_t ref;
  RETURN_IF_ERROR(callstack_get_local_addr(index, &ref));
  RETURN_IF_ERROR(callstack_push_operand(ref));
  return ERROR_NONE;
}

static error_code_e op_lda_a(FILE *f, char l) {
  int32_t index = bc_read_int();
  DBG("LDA\tA(%d)", index);
  csval_t ref;
  RETURN_IF_ERROR(callstack_get_arg_addr(index, &ref));
  RETURN_IF_ERROR(callstack_push_operand(ref));
  return ERROR_NONE;
}

static error_code_e op_lda_c(FILE *f, char l) {
  int32_t index = bc_read_int();
  aint clos = cs_clos();
  if (UNBOXED(clos) && UNBOX(clos) == 0) {
    DBG("LDA\tC(%d): no closure in current frame\n", index);
    return ERROR_NO_CLOSURE_IN_CURRENT_FRAME;
  }

  DBG("LDA\tC(%d)", index);
  aint *slot_addr = closure_capture_slot_addr(clos, (uint32_t)index);
  RETURN_IF_ERROR(callstack_push_operand(csval_extern(slot_addr)));
  return ERROR_NONE;
}

static error_code_e op_st_g(FILE *f, char l) {
  csval_t val;
  int index = bc_read_int();
  DBG("ST\tG(%d)", index);
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  RETURN_IF_ERROR(callstack_set_glob(index, val));
  RETURN_IF_ERROR(callstack_push_operand(val)); /* Push back onto stack */
  return ERROR_NONE;
}

static error_code_e op_st_l(FILE *f, char l) {
  csval_t val;
  int index = bc_read_int();
  DBG("ST\tL(%d)", index);
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  RETURN_IF_ERROR(callstack_set_local(index, val));
  RETURN_IF_ERROR(callstack_push_operand(val)); /* Push back onto stack */
  return ERROR_NONE;
}

static error_code_e op_st_a(FILE *f, char l) {
  csval_t val;
  int index = bc_read_int();
  DBG("ST\tA(%d)", index);
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  RETURN_IF_ERROR(callstack_set_arg(index, val));
  RETURN_IF_ERROR(callstack_push_operand(val)); /* Push back onto stack */
  return ERROR_NONE;
}

static error_code_e op_st_c(FILE *f, char l) {
  int32_t index = bc_read_int();
  csval_t val;
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  aint a;
  a = csval_to_aint(val);

  aint clos = cs_clos();
  if (UNBOXED(clos) && UNBOX(clos) == 0) {
    DBG("ST\tC(%d): no closure in current frame\n", index);
    return ERROR_NO_CLOSURE_IN_CURRENT_FRAME;
  }

  DBG("ST\tC(%d)", index);
  aint *slot_addr = closure_capture_slot_addr(clos, (uint32_t)index);
  *slot_addr = a;
  RETURN_IF_ERROR(callstack_push_operand(val));
  return ERROR_NONE;
}

static error_code_e op_cjmpz(FILE *f, char l) {
  csval_t val;
  aint cond;
  int32_t l_offset = bc_read_int();
  DBG("CJMPz\t0x%.8x", l_offset);
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  RETURN_IF_ERROR(csval_to_imm_aint(val, &cond));

  if (!UNBOX(cond))
    RETURN_IF_ERROR(ip_jmp(l_offset));
  return ERROR_NONE;
}

static error_code_e op_cjmpnz(FILE *f, char l) {
  csval_t val;
  aint cond;
  int32_t l_offset = bc_read_int();
  DBG("CJMPnz\t0x%.8x", l_offset);
  RETURN_IF_ERROR(callstack_pop_operand(&val));
  RETURN_IF_ERROR(csval_to_imm_aint(val, &cond));

  if (UNBOX(cond))
    RETURN_IF_ERROR(ip_jmp(l_offset));
  return ERROR_NONE;
}

static error_code_e op_begin(FILE *f, char l) {
  int nargs = bc_read_int();
  int nlocals = bc_read_int();
  if ((uint32_t)nargs != (uint32_t)cs_nargs()) {
    failure("BEGIN:\t nargs mismatch: exp: %d act: %d\n", cs_nargs(), nargs);
    return ERROR_NARGS_MISMATCH;
  }
  DBG("BEGIN\t%d %d", nargs, nlocals);
  RETURN_IF_ERROR(cs_alloc_locals(nlocals));
  return ERROR_NONE;
}

static error_code_e op_cbegin(FILE *f, char l) {
  int nargs = bc_read_int();
  int nlocals = bc_read_int();
  if ((uint32_t)nargs != (uint32_t)cs_nargs()) {
    failure("CBEGIN:\t nargs mismatch: exp: %d act: %d\n", cs_nargs(), nargs);
    return ERROR_NARGS_MISMATCH;
  }
  DBG("CBEGIN\t%d\t%d", nargs, nlocals);
  RETURN_IF_ERROR(cs_alloc_locals((uint32_t)nlocals));
  return ERROR_NONE;
}

static error_code_e op_closure(FILE *f, char l) {
  int32_t l_offset = bc_read_int();
  DBG("CLOSURE\t0x%.8x", l_offset);

  int n = bc_read_int();
  if (n < 0) {
    return ERROR_INVALID_CAPTURE_TYPE;
  }

  data *r = (data *)alloc_closure((uint32_t)n + 1);
  push_extra_root((void **)&r);

  ((void **)r->contents)[0] = (void *)(ip_base() + l_offset);

  for (int i = 0; i < n; i++) {
    switch (bc_read_byte()) {
    case OP_L_G: {
      uint32_t index = bc_read_int();
      DBG(" G(%d)", index);
      csval_t v_val;
      RETURN_IF_ERROR(callstack_get_glob(index, &v_val));
      ((aint *)r->contents)[i + 1] = csval_to_aint(v_val);
    } break;

    case OP_L_L: {
      uint32_t index = bc_read_int();
      DBG(" L(%d)", index);
      csval_t v_val;
      RETURN_IF_ERROR(callstack_get_local(index, &v_val));
      ((aint *)r->contents)[i + 1] = csval_to_aint(v_val);
    } break;

    case OP_L_A: {
      uint32_t index = (uint32_t)bc_read_int();
      DBG(" A(%d)", index);
      csval_t v_val;
      RETURN_IF_ERROR(callstack_get_arg(index, &v_val));
      ((aint *)r->contents)[i + 1] = csval_to_aint(v_val);
    } break;

    case OP_L_C: {
      uint32_t index = (uint32_t)bc_read_int();
      DBG(" C(%d)", index);
      aint clos = cs_clos();
      if (UNBOXED(clos) && UNBOX(clos) == 0) {
        failure("CLOSURE: capture C(%u) but no closure in current frame\n",
                index);
      }
      ((aint *)r->contents)[i + 1] = closure_capture_ref(clos, index);
    } break;

    default:
      return ERROR_INVALID_CAPTURE_TYPE;
    }
  }

  pop_extra_root((void **)&r);
  RETURN_IF_ERROR(callstack_push_operand(csval_extern((aint *)r->contents)));
  return ERROR_NONE;
}

static error_code_e op_callc(FILE *f, char l) {
  uint32_t n = (uint32_t)bc_read_int();
  uint32_t ret_off = ip_offset();

  // stack: ... [closure][arg0]...[arg(n-1)]
  csval_t clos_ref = callstack_operands_tail_ref(n + 1 /* [clos][n args] */);
  csval_t *clos_ptr = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(clos_ref, (aint **)&clos_ptr));
  aint clos = csval_to_aint(*clos_ptr);
  if (n > 0)
    memmove(clos_ptr, clos_ptr + 1, n * CS_SLOT_BYTES);
  RETURN_IF_ERROR(callstack_pop_operand(NULL));

  void *entry = closure_entry_ptr(clos);

  DBG("CALLC\t%d", n);
  RETURN_IF_ERROR(cs_push_cframe(clos, ret_off, (uint32_t)n));
  RETURN_IF_ERROR(ip_jmp((int32_t)((char *)entry - ip_base())));
  return ERROR_NONE;
}

static error_code_e op_call(FILE *f, char l) {
  int offset = bc_read_int();
  int nargs = bc_read_int();
  DBG("CALL\t0x%.8x %d", offset, nargs);
  RETURN_IF_ERROR(cs_push_frame(ip_offset(), (uint32_t)nargs));
  RETURN_IF_ERROR(ip_jmp(offset));
  return ERROR_NONE;
}

static error_code_e op_tag(FILE *f, char l) {
  const char *tag = bc_read_string();
  int arity = bc_read_int();

  csval_t p_val;
  RETURN_IF_ERROR(callstack_pop_operand(&p_val));
  aint p = csval_to_aint(p_val);
  aint th = LtagHash((char *)tag);
  aint an = BOX(arity);
  DBG("TAG\t%s %d", tag, arity);

  aint r;
  if (arity == 0 && UNBOXED(p)) {
    /* immediate constructor  */
    r = (UNBOX(p) == UNBOX(th)) ? BOX(1) : BOX(0);
  } else {
    /* sexp / array / other */
    r = Btag((void *)p, th, an);
  }
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(r)));
  return ERROR_NONE;
}

static error_code_e op_array(FILE *f, char l) {
  int size = bc_read_int();
  csval_t p_val;
  RETURN_IF_ERROR(callstack_pop_operand(&p_val));
  aint p = csval_to_aint(p_val);
  DBG("ARRAY\t%d %" PRIdAI, size, UNBOX(p));
  aint r = Barray_patt((void *)p, BOX(size));
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(r)));
  return ERROR_NONE;
}

static error_code_e op_fail(FILE *f, char l) {
  int line = bc_read_int();
  int col = bc_read_int();
  char mainf[] = "main";

  csval_t p_val;
  RETURN_IF_ERROR(callstack_pop_operand(&p_val));
  aint p = csval_to_aint(p_val);
  DBG("FAIL\t%d %d", line, col);
  Bmatch_failure((void *)p, mainf, BOX(line), BOX(col));
  return ERROR_NONE;
}

static error_code_e op_line(FILE *f, char l) {
  int line = bc_read_int();
  DBG("LINE\t%d", line);
  return ERROR_NONE;
}

static error_code_e op_patt(FILE *f, char l) {
  csval_t p_val;
  RETURN_IF_ERROR(callstack_pop_operand(&p_val));

  switch (l) {
  case OP_L_PATT_STR: {
    csval_t p2_val;
    RETURN_IF_ERROR(callstack_pop_operand(&p2_val));
    aint p = csval_to_aint(p_val);
    aint p2 = csval_to_aint(p2_val);

    DBG("PATT\t=str\t%lld %lld", UNBOX(p), UNBOX(p2));
    aint r = Bstring_patt((void *)p, (void *)p2);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case OP_L_PATT_STRING_TAG: {
    aint p = csval_to_aint(p_val);
    aint r = Bstring_tag_patt((void *)p);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case OP_L_PATT_ARRAY_TAG: {
    aint p = csval_to_aint(p_val);
    DBG("PATT\t#array\t%lld", UNBOX(p));
    aint r = Barray_tag_patt((void *)p);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case OP_L_PATT_SEXP_TAG: {
    aint p = csval_to_aint(p_val);
    DBG("PATT\t#sexp\t%lld", UNBOX(p));
    aint r = Bsexp_tag_patt((void *)p);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case OP_L_PATT_REF: {
    aint r = (p_val.ty == CS_INTERNAL_REF) ? BOX(1) : BOX(0);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case OP_L_PATT_VAL: {
    aint r = (p_val.ty == CS_IMM) ? BOX(1) : BOX(0);

    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case OP_L_PATT_FUN: {
    aint p = csval_to_aint(p_val);
    DBG("PATT\t#fun\t%lld", UNBOX(p));
    aint r = Bclosure_tag_patt((void *)p);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  default:
    return op_invalid(f, l);
  }
  return ERROR_NONE;
}

static error_code_e op_call_lread(FILE *f, char l) {
  DBG("CALL\tLread");
  fprintf(stdout, " ");
  aint r = Lread();
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(r)));
  return ERROR_NONE;
}

static error_code_e op_call_lwrite(FILE *f, char l) {
  DBG("CALL\tLwrite");
  csval_t v;
  RETURN_IF_ERROR(callstack_pop_operand(&v));
  aint n = csval_to_aint(v);
  aint r = Lwrite(n);
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(r)));
  return ERROR_NONE;
}

static error_code_e op_call_llength(FILE *f, char l) {
  DBG("CALL\tLlength");
  csval_t v;
  RETURN_IF_ERROR(callstack_pop_operand(&v));
  aint p = csval_to_aint(v);
  if (UNBOXED(p)) {
    return ERROR_NOT_REF;
  }
  aint r = Llength((void *)p);
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(r)));
  return ERROR_NONE;
}

static error_code_e op_call_lstring(FILE *f, char l) {
  DBG("CALL\tLstring");
  csval_t v;
  RETURN_IF_ERROR(callstack_pop_operand(&v));
  aint p = csval_to_aint(v);
  aint args[1] = {p};
  void *r = Lstring(args);
  RETURN_IF_ERROR(callstack_push_operand(csval_extern((aint *)r)));
  return ERROR_NONE;
}

static error_code_e op_call_barray(FILE *f, char l) {
  int size = bc_read_int();
  DBG("CALL\tBarray\t%d", size);
  if (size < 0) {
    return ERROR_STACK_UNDERFLOW;
  }
  if (size == 0) {
    void *r = Barray(NULL, BOX(0));
    RETURN_IF_ERROR(callstack_push_operand(csval_extern((aint *)r)));
    return ERROR_NONE;
  }
  csval_t args_ref = callstack_operands_tail_ref((uint32_t)size);
  aint *slot_words = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(args_ref, &slot_words));

  data *r = (data *)alloc_array((uint32_t)size);
  for (uint32_t i = 0; i < (uint32_t)size; i++) {
    csval_t v = csval_from_slot_words(slot_words + (i * CSVAL_WORDS));
    ((aint *)r->contents)[i] = csval_to_aint(v);
  }

  RETURN_IF_ERROR(callstack_pop_n_operands(
      (uint32_t)size)); // Shrink bottom n operands used before
  RETURN_IF_ERROR(callstack_push_operand(csval_extern((aint *)r->contents)));
  return ERROR_NONE;
}

void interpret_bc(FILE *f, error_code_e *error_code) {
  char *base_ip = __ip;

  for (;;) {
    char x = bc_read_byte();
    char h = (x & 0xF0) >> 4;
    char l = x & 0x0F;

    DBG("0x%.8lx:\t", __ip - base_ip - 1);
    current_h = h;

    switch (h) {
    case OP_H_STOP:
      *error_code = ERROR_STOP;
      goto done;

    case OP_H_BINOP:
      *error_code = op_binop(f, l);
      break;

    case OP_H_OP1:
      switch (l) {
      case OP_L_CONST:
        *error_code = op_const(f, l);
        break;
      case OP_L_STRING:
        *error_code = op_string(f, l);
        break;
      case OP_L_SEXP:
        *error_code = op_sexp(f, l);
        break;
      case OP_L_STI:
        *error_code = op_sti(f, l);
        break;
      case OP_L_STA:
        *error_code = op_sta(f, l);
        break;
      case OP_L_JMP:
        *error_code = op_jmp(f, l);
        break;
      case OP_L_END:
        *error_code = op_end(f, l);
        break;
      case OP_L_RET:
        *error_code = op_end(f, l);
        break;
      case OP_L_DROP:
        *error_code = op_drop(f, l);
        break;
      case OP_L_DUP:
        *error_code = op_dup(f, l);
        break;
      case OP_L_SWAP:
        *error_code = op_swap(f, l);
        break;
      case OP_L_ELEM:
        *error_code = op_elem(f, l);
        break;
      default:
        *error_code = op_invalid(f, l);
        break;
      }
      break;

    case OP_H_LD:
    case OP_H_LDA:
    case OP_H_ST:
      switch (l) {
      case OP_L_G:
        *error_code = h == OP_H_LD    ? op_ld_g(f, l)
                      : h == OP_H_LDA ? op_lda_g(f, l)
                                      : op_st_g(f, l);
        break;
      case OP_L_L:
        *error_code = h == OP_H_LD    ? op_ld_l(f, l)
                      : h == OP_H_LDA ? op_lda_l(f, l)
                                      : op_st_l(f, l);
        break;
      case OP_L_A:
        *error_code = h == OP_H_LD    ? op_ld_a(f, l)
                      : h == OP_H_LDA ? op_lda_a(f, l)
                                      : op_st_a(f, l);
        break;
      case OP_L_C:
        *error_code = h == OP_H_LD    ? op_ld_c(f, l)
                      : h == OP_H_LDA ? op_lda_c(f, l)
                                      : op_st_c(f, l);
        break;
      default:
        *error_code = op_invalid(f, l);
        break;
      }
      break;

    case OP_H_OP5:
      switch (l) {
      case OP_L_CJMPz:
        *error_code = op_cjmpz(f, l);
        break;
      case OP_L_CJMPnz:
        *error_code = op_cjmpnz(f, l);
        break;
      case OP_L_BEGIN:
        *error_code = op_begin(f, l);
        break;
      case OP_L_CBEGIN:
        *error_code = op_cbegin(f, l);
        break;
      case OP_L_CLOSURE:
        *error_code = op_closure(f, l);
        break;
      case OP_L_CALLC:
        *error_code = op_callc(f, l);
        break;
      case OP_L_CALL:
        *error_code = op_call(f, l);
        break;
      case OP_L_TAG:
        *error_code = op_tag(f, l);
        break;
      case OP_L_ARRAY:
        *error_code = op_array(f, l);
        break;
      case OP_L_FAIL:
        *error_code = op_fail(f, l);
        break;
      case OP_L_LINE:
        *error_code = op_line(f, l);
        break;
      default:
        *error_code = op_invalid(f, l);
        break;
      }
      break;

    case OP_H_PATT:
      *error_code = op_patt(f, l);
      break;

    case OP_H_BUILTIN:
      switch (l) {
      case OP_L_LREAD:
        *error_code = op_call_lread(f, l);
        break;
      case OP_L_LWRITE:
        *error_code = op_call_lwrite(f, l);
        break;
      case OP_L_LLENGTH:
        *error_code = op_call_llength(f, l);
        break;
      case OP_L_LSTRING:
        *error_code = op_call_lstring(f, l);
        break;
      case OP_L_BARRAY:
        *error_code = op_call_barray(f, l);
        break;
      default:
        *error_code = op_invalid(f, l);
        break;
      }
      break;

    default:
      *error_code = op_invalid(f, l);
      break;
    }

    if (*error_code != ERROR_NONE)
      break;
    DBG("\n");
  }
done:
  DBG("<done>\n");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <bytecode_file>\n", argv[0]);
    return 1;
  }

  if (run_verify(argv[1]) != 0) {
    fprintf(stderr, "Verification failed!\n");
    return 1;
  }

  /* Load bytecode */
  __bf = read_file(argv[1]);
  if (!__bf) {
    return 1;
  }

  /* Virtual regs */
  __ip = __bf->code_ptr;

  /* Create call stack */
  cs_init(__bf->global_area_size);

  /* Interpret bytecode */
  error_code_e error_code = ERROR_NONE;
  interpret_bc(stdout, &error_code);
  if (error_code != ERROR_NONE && error_code != ERROR_STOP) {
    fprintf(stderr, "Error: %d\n", error_code);
    return 1;
  }

  /* Cleanup */
  free(__bf);

  return 0;
}

// Done
/*
- Stack size propagation in Basic Block
- Stack size propagation in with JMP, CJMPz, CJMPnz, CALLC, CALL
*/

// TODO
/*
- Убрать из validate std::to_string
- Проверка что взяли в String строку с валидным id
- STA/STI
- Проверка выхода за граница кода с помощью jmp, call и проч.
- Проверка выхода за границы кода в процессе обхода
- ERROR_OPND_STACK_UNDERFLOW, ERROR_STACK_UNDERFLOW
- ERROR_GLOB_IDX_NEGATIVE, ERROR_GLOB_IDX_OUT_OF_RANGE
- ERROR_NARGS_MISMATCH
*/