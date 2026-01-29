#include <stdint.h>
#include <stdio.h>

#include "../runtime/gc.h"
#include "../runtime/runtime.h"

#include "bytecode.h"
#include "state.h"

/* Virtual regs */
char *__ip = NULL;  /* address of current instruction */
size_t __cs_fp = 0; /* slot index of nlocals for current frame */
size_t __cs_sp = 0; /* slot index of next free entry */

/* Internal */
size_t __cs_cap = 0;
size_t __cs_nframes = 0;
size_t __cs_nglob = 0;
aint *__cs_ram_layout = NULL;
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

typedef error_code_e (*op_handler)(FILE *f, char l);

static const char *ops[] = {
    "+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!=", "&&", "!!"};
static const char *pats[] = {"=str", "#string", "#array", "#sexp",
                             "#ref", "#val",    "#fun"};
static const char *lds[] = {"LD", "LDA", "ST"};

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
  if (l == 0 || l > (char)(sizeof(ops) / sizeof(ops[0]))) {
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
  case 1:
    result_unbox = a + b;
    break; /* + */
  case 2:
    result_unbox = a - b;
    break; /* - */
  case 3:
    result_unbox = a * b;
    break; /* * */
  case 4:
    if (b == 0) {
      failure("integer division by zero at ip=0x%.8lx\n",
              (unsigned long)ip_offset());
    }
    result_unbox = a / b;
    break; /* / */
  case 5:
    if (b == 0) {
      failure("integer modulo by zero at ip=0x%.8lx\n",
              (unsigned long)ip_offset());
    }
    result_unbox = a % b;
    break; /* % */
  case 6:
    result_unbox = a < b;
    break; /* < */
  case 7:
    result_unbox = a <= b;
    break; /* <= */
  case 8:
    result_unbox = a > b;
    break; /* > */
  case 9:
    result_unbox = a >= b;
    break; /* >= */
  case 10:
    result_unbox = a == b;
    break; /* == */
  case 11:
    result_unbox = a != b;
    break; /* != */
  case 12:
    result_unbox = a && b;
    break; /* && */
  case 13:
    result_unbox = a || b;
    break; /* !! */
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
  csval_t args_ref;
  const char *tag = bc_read_string();
  int32_t arity = bc_read_int();
  DBG("SEXP\t%s %d", tag, arity);
  aint th = LtagHash((char *)tag);
  csval_t pushed_tag = csval_from_aint(th);
  RETURN_IF_ERROR(callstack_push_operand(pushed_tag));
  RETURN_IF_ERROR(callstack_pop_n_operands(arity + 1, &args_ref));
  aint *slot_words = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(args_ref, &slot_words));
  uint32_t nargs = (uint32_t)(arity + 1);
  aint args[nargs];
  for (uint32_t i = 0; i < nargs; i++) {
    csval_t v = csval_from_slot_words(slot_words + (i * CSVAL_WORDS));
    args[i] = csval_to_aint(v);
  }
  void *r = Bsexp(args, BOX(nargs /* With tag*/));
  RETURN_IF_ERROR(callstack_push_operand(csval_extern((aint *)r)));
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
  RETURN_IF_ERROR(callstack_pop_operand(&callee_ret));
  RETURN_IF_ERROR(cs_pop_frame(&ret_off));
  if (__cs_nframes == 0) {
    return ERROR_STOP;
  }
  DBG("END\t%u", ret_off);
  RETURN_IF_ERROR(
      callstack_push_operand(callee_ret)); // Put retval on caller stack
  RETURN_IF_ERROR(ip_jmp(ret_off));
  return ERROR_NONE;
}

static error_code_e op_ret(FILE *f, char l) {
  csval_t callee_ret;
  uint32_t ret_off;
  RETURN_IF_ERROR(callstack_pop_operand(&callee_ret));
  RETURN_IF_ERROR(cs_pop_frame(&ret_off));
  if (__cs_nframes == 0) {
    return ERROR_STOP;
  }
  DBG("RET\t%u", ret_off);
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
  aint ref = closure_capture_ref(clos, (uint32_t)index);
  aint *cell = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(csval_extern((aint *)ref), &cell));
  RETURN_IF_ERROR(callstack_push_operand(csval_from_aint(*cell)));
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
  aint ref = closure_capture_ref(clos, (uint32_t)index);
  RETURN_IF_ERROR(
      callstack_push_operand(csval_extern((aint *)ref))); // direct ref
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
  aint ref = closure_capture_ref(clos, (uint32_t)index);
  aint *cell = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(csval_extern((aint *)ref), &cell));
  *cell = a;

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
  DBG("BEGIN\t%d %d", nargs, nlocals);
  RETURN_IF_ERROR(cs_alloc_locals(nlocals));
  return ERROR_NONE;
}

static error_code_e op_cbegin(FILE *f, char l) {
  int nargs = bc_read_int();
  int nlocals = bc_read_int();
  if (__cs_nframes == 0)
    RETURN_IF_ERROR(cs_push_frame(0, (uint32_t)nargs));
  if ((uint32_t)nargs != (uint32_t)cs_nargs()) {
    DBG("CBEGIN\t%d\t%d: nargs mismatch\n", nargs, nlocals);
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

  // args[0] = entry pointer, args[1..n] = captured refs
  aint *args = alloca(sizeof(aint) * (n + 1));
  args[0] = (aint)(ip_base() + l_offset);

  for (int i = 0; i < n; i++) {
    switch (bc_read_byte()) {
    case 0: { // G(m)
      uint32_t index = bc_read_int();
      DBG(" G(%d)", index);
      /* Capture argument by stable heap cell (value at closure creation
       * time). */
      csval_t v_val;
      aint v;
      RETURN_IF_ERROR(callstack_get_glob(index, &v_val));
      v = csval_to_aint(v_val);
      aint *cell = alloc_capture_cell(v);
      args[i + 1] = (aint)cell;
    } break;

    case 1: { // L(m)
      uint32_t index = bc_read_int();
      DBG(" L(%d)", index);
      /* Capture local by stable heap cell (value at closure creation
       * time). */
      csval_t v_val;
      aint v;
      RETURN_IF_ERROR(callstack_get_local(index, &v_val));
      v = csval_to_aint(v_val);
      aint *cell = alloc_capture_cell(v);
      args[i + 1] = (aint)cell; /* direct-address ref */
    } break;

    case 2: { // A(m)
      uint32_t index = (uint32_t)bc_read_int();
      DBG(" A(%d)", index);
      /* Capture argument by stable heap cell (value at closure creation
       * time). */
      csval_t v_val;
      aint v;
      RETURN_IF_ERROR(callstack_get_arg(index, &v_val));
      v = csval_to_aint(v_val);
      aint *cell = alloc_capture_cell(v);
      args[i + 1] = (aint)cell; /* direct-address ref */
    } break;

    case 3: { // C(m)
      uint32_t index = (uint32_t)bc_read_int();
      DBG(" C(%d)", index);

      aint clos = cs_clos();
      if (UNBOXED(clos) && UNBOX(clos) == 0) {
        failure("CLOSURE: capture C(%u) but no closure in current frame\n",
                index);
      }

      // берём ref на captured cell из текущего closure
      args[i + 1] = closure_capture_ref(clos, index);
    } break;

    default:
      // DBG("CLOSURE: invalid capture type %d\n", kind);
      return ERROR_INVALID_CAPTURE_TYPE;
    }
  }

  aint clos_obj = (aint)Bclosure(args, BOX(n));
  RETURN_IF_ERROR(callstack_push_operand(csval_extern((aint *)clos_obj)));
  return ERROR_NONE;
}

static error_code_e op_callc(FILE *f, char l) {
  uint32_t n = (uint32_t)bc_read_int();
  uint32_t ret_off = ip_offset();

  // stack: ... [closure][arg0]...[arg(n-1)] (top = arg(n-1))
  csval_t *tmp = alloca(sizeof(csval_t) * (size_t)n);

  for (int i = (int)n - 1; i >= 0; --i) {
    RETURN_IF_ERROR(callstack_pop_operand(&tmp[i]));
  }

  csval_t clos_val;
  RETURN_IF_ERROR(callstack_pop_operand(&clos_val));
  aint clos = csval_to_aint(clos_val);

  for (uint32_t i = 0; i < n; ++i) {
    RETURN_IF_ERROR(callstack_push_operand(tmp[i]));
  }

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
  case 0: { /* PATT =str */
    csval_t p2_val;
    RETURN_IF_ERROR(callstack_pop_operand(&p2_val));
    aint p = csval_to_aint(p_val);
    aint p2 = csval_to_aint(p2_val);

    DBG("PATT\t=str\t%lld %lld", UNBOX(p), UNBOX(p2));
    aint r = Bstring_patt((void *)p, (void *)p2);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case 1: { /* PATT #string */
    aint p = csval_to_aint(p_val);
    aint r = Bstring_tag_patt((void *)p);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case 2: { /* PATT #array */
    aint p = csval_to_aint(p_val);
    DBG("PATT\t#array\t%lld", UNBOX(p));
    aint r = Barray_tag_patt((void *)p);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case 3: { /* PATT #sexp */
    aint p = csval_to_aint(p_val);
    DBG("PATT\t#sexp\t%lld", UNBOX(p));
    aint r = Bsexp_tag_patt((void *)p);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case 4: { /* PATT #ref */
    aint r = (p_val.ty == CS_INTERNAL_REF) ? BOX(1) : BOX(0);
    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case 5: { /* PATT #val */
    aint r = (p_val.ty == CS_IMM) ? BOX(1) : BOX(0);

    csval_t pushed = csval_from_aint(r);
    RETURN_IF_ERROR(callstack_push_operand(pushed));
  } break;

  case 6: { /* PATT #fun */
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
  csval_t args_ref;
  RETURN_IF_ERROR(callstack_pop_n_operands((uint32_t)size, &args_ref));
  aint *slot_words = NULL;
  RETURN_IF_ERROR(csval_to_ref_aintp(args_ref, &slot_words));
  aint args[(size_t)size];
  for (uint32_t i = 0; i < (uint32_t)size; i++) {
    csval_t v = csval_from_slot_words(slot_words + (i * CSVAL_WORDS));
    args[i] = csval_to_aint(v);
  }
  void *r = Barray(args, BOX(size));
  RETURN_IF_ERROR(callstack_push_operand(csval_extern((aint *)r)));
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

void interpret_bc(FILE *f, error_code_e *error_code) {
  char *base_ip = __ip;
  static op_handler handlers[16][16];
  init_handlers(handlers);

  for (;;) {
    char x = bc_read_byte();
    char h = (x & 0xF0) >> 4;
    char l = x & 0x0F;

    DBG("0x%.8lx:\t", __ip - base_ip - 1);
    current_h = h;
    if ((*error_code = handlers[h][l](f, l)) != ERROR_NONE) {
      break;
    }
    DBG("\n");
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <bytecode_file>\n", argv[0]);
    return 1;
  }

  /* Load bytecode */
  __bf = parse_bc_file(argv[1]);
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