#include <stdint.h>
#include <stdio.h>

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
  return ERROR_NONE;
}

static error_code_e op_sexp(FILE *f, struct interpreter_state_t *state,
                            char l) {
  char *value = state_read_string(state);
  int size = state_read_int(state);
  DBG("SEXP\t%s ", value);
  DBG("%d", size);
  return ERROR_NONE;
}

static error_code_e op_sti(FILE *f, struct interpreter_state_t *state, char l) {
  DBG("STI");
  return ERROR_NONE;
}

static error_code_e op_sta(FILE *f, struct interpreter_state_t *state, char l) {
  DBG("STA");
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
  DBG("ELEM");
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
  const char *op_name = lds[0];
  int index = state_read_int(state);
  DBG("%s\tC(%d)", op_name, index);
  return ERROR_NONE;
}

static error_code_e op_lda_g(FILE *f, struct interpreter_state_t *state,
                             char l) {
  const char *op_name = lds[1];
  int index = state_read_int(state);
  DBG("%s\tG(%d)", op_name, index);
  return ERROR_NONE;
}

static error_code_e op_lda_l(FILE *f, struct interpreter_state_t *state,
                             char l) {
  const char *op_name = lds[1];
  int index = state_read_int(state);
  DBG("%s\tL(%d)", op_name, index);
  return ERROR_NONE;
}

static error_code_e op_lda_a(FILE *f, struct interpreter_state_t *state,
                             char l) {
  const char *op_name = lds[1];
  int index = state_read_int(state);
  DBG("%s\tA(%d)", op_name, index);
  return ERROR_NONE;
}

static error_code_e op_lda_c(FILE *f, struct interpreter_state_t *state,
                             char l) {
  const char *op_name = lds[1];
  int index = state_read_int(state);
  DBG("%s\tC(%d)", op_name, index);
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
  const char *op_name = lds[2];
  int index = state_read_int(state);
  DBG("%s\tC(%d)", op_name, index);
  return ERROR_NONE;
}

static error_code_e op_cjmpz(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int target = state_read_int(state);
  DBG("CJMPz\t0x%.8x", target);
  return ERROR_NONE;
}

static error_code_e op_cjmpnz(FILE *f, struct interpreter_state_t *state,
                              char l) {
  int target = state_read_int(state);
  DBG("CJMPnz\t0x%.8x", target);
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
  int offset = state_read_int(state);
  DBG("CLOSURE\t0x%.8x", offset);
  int n = state_read_int(state);
  for (int i = 0; i < n; i++) {
    unsigned char kind = state_read_byte(state);
    switch (kind) {
    case 0: {
      int index = state_read_int(state);
      DBG("G(%d)", index);
    } break;
    case 1: {
      int index = state_read_int(state);
      DBG("L(%d)", index);
    } break;
    case 2: {
      int index = state_read_int(state);
      DBG("A(%d)", index);
    } break;
    case 3: {
      int index = state_read_int(state);
      DBG("C(%d)", index);
    } break;
    default:
      return op_invalid(f, state, l);
    }
  }
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
  int arity = state_read_int(state);
  DBG("CALL\t0x%.8x ", offset);
  DBG("%d", arity);
  return ERROR_NONE;
}

static error_code_e op_tag(FILE *f, struct interpreter_state_t *state, char l) {
  char *tag = state_read_string(state);
  int arity = state_read_int(state);
  DBG("TAG\t%s ", tag);
  DBG("%d", arity);
  return ERROR_NONE;
}

static error_code_e op_array(FILE *f, struct interpreter_state_t *state,
                             char l) {
  int size = state_read_int(state);
  DBG("ARRAY\t%d", size);
  return ERROR_NONE;
}

static error_code_e op_fail(FILE *f, struct interpreter_state_t *state,
                            char l) {
  int code = state_read_int(state);
  int value = state_read_int(state);
  DBG("FAIL\t%d", code);
  DBG("%d", value);
  return ERROR_NONE;
}

static error_code_e op_line(FILE *f, struct interpreter_state_t *state,
                            char l) {
  int line = state_read_int(state);
  DBG("LINE\t%d", line);
  return ERROR_NONE;
}

static error_code_e op_patt_str(FILE *f, struct interpreter_state_t *state,
                                char l) {
  const char *pattern = pats[0];
  DBG("PATT\t%s", pattern);
  return ERROR_NONE;
}

static error_code_e op_patt_string(FILE *f, struct interpreter_state_t *state,
                                   char l) {
  const char *pattern = pats[1];
  DBG("PATT\t%s", pattern);
  return ERROR_NONE;
}

static error_code_e op_patt_array(FILE *f, struct interpreter_state_t *state,
                                  char l) {
  const char *pattern = pats[2];
  DBG("PATT\t%s", pattern);
  return ERROR_NONE;
}

static error_code_e op_patt_sexp(FILE *f, struct interpreter_state_t *state,
                                 char l) {
  const char *pattern = pats[3];
  DBG("PATT\t%s", pattern);
  return ERROR_NONE;
}

static error_code_e op_patt_ref(FILE *f, struct interpreter_state_t *state,
                                char l) {
  const char *pattern = pats[4];
  DBG("PATT\t%s", pattern);
  return ERROR_NONE;
}

static error_code_e op_patt_val(FILE *f, struct interpreter_state_t *state,
                                char l) {
  const char *pattern = pats[5];
  DBG("PATT\t%s", pattern);
  return ERROR_NONE;
}

static error_code_e op_patt_fun(FILE *f, struct interpreter_state_t *state,
                                char l) {
  const char *pattern = pats[6];
  DBG("PATT\t%s", pattern);
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

  handlers[6][0] = &op_patt_str;
  handlers[6][1] = &op_patt_string;
  handlers[6][2] = &op_patt_array;
  handlers[6][3] = &op_patt_sexp;
  handlers[6][4] = &op_patt_ref;
  handlers[6][5] = &op_patt_val;
  handlers[6][6] = &op_patt_fun;

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