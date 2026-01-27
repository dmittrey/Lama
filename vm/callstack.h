#ifndef CALLSTACK_H
#define CALLSTACK_H

#include "error.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime_common.h"

#define CSVAL_WORDS 2

/* Opaque */
struct callstack_t;

/* Callstack value */
typedef enum csval_type_e {
  CS_INTERNAL_REF = 0,
  CS_EXTERNAL_REF = 1,
  CS_IMM = 2,
} csval_type_e;
typedef struct csval_t {
  csval_type_e ty;
  aint val; // Boxed(external aint) || Unboxed(internal ref idx or imm)
} csval_t;
static inline csval_t csval_imm(aint imm) {
  assert(UNBOXED(imm));
  return (csval_t){.ty = CS_IMM, .val = imm};
}
static inline csval_t csval_extern(aint ref) {
  assert(!UNBOXED(ref));
  return (csval_t){.ty = CS_EXTERNAL_REF, .val = ref};
}
static inline csval_t csval_intern(aint offset) {
  assert(UNBOXED(offset));
  return (csval_t){.ty = CS_INTERNAL_REF, .val = offset};
}
error_code_e csval_to_imm(struct callstack_t *stack, csval_t val, aint *ret);
error_code_e csval_to_ref(struct callstack_t *stack, csval_t val, aint **ret);

/* Helpers */
uint32_t callstack_nlocals(struct callstack_t *stack);
uint32_t callstack_noperands(struct callstack_t *stack);
uint32_t callstack_nargs(struct callstack_t *s);
size_t callstack_nframes(struct callstack_t *s);
aint callstack_closure(struct callstack_t *s);

/* Lifecycle */
struct callstack_t *create_callstack(int nglobals);
void destroy_callstack(struct callstack_t *);

/* Frame operations */
error_code_e callstack_push_frame(struct callstack_t *stack, uint32_t ret_off,
                                  uint32_t nargs);
error_code_e callstack_push_cframe(struct callstack_t *stack, aint closure,
                                   uint32_t ret_off, uint32_t nargs);
error_code_e callstack_alloc_locals(struct callstack_t *stack,
                                    uint32_t nlocals);
error_code_e callstack_pop_frame(struct callstack_t *stack, uint32_t *ret_off);

/* Access arguments and locals */
error_code_e callstack_get_local(struct callstack_t *stack, uint32_t index,
                                 csval_t *ret);
error_code_e callstack_set_local(struct callstack_t *stack, uint32_t index,
                                 csval_t value);
error_code_e callstack_get_arg(struct callstack_t *stack, uint32_t index,
                               csval_t *ret);
error_code_e callstack_set_arg(struct callstack_t *stack, uint32_t index,
                               csval_t value);
error_code_e callstack_get_glob(struct callstack_t *stack, uint32_t index,
                                csval_t *ret);
error_code_e callstack_set_glob(struct callstack_t *stack, uint32_t index,
                                csval_t value);

/* Operands stack */
error_code_e callstack_pop_operand(struct callstack_t *stack, csval_t *ret);
error_code_e callstack_push_operand(struct callstack_t *stack, csval_t value);
error_code_e callstack_pop_n_operands(struct callstack_t *stack, uint32_t n,
                                      csval_t *ret);

/* Reference */
error_code_e callstack_get_local_addr(struct callstack_t *stack, uint32_t index,
                                      csval_t *ret);
error_code_e callstack_get_arg_addr(struct callstack_t *stack, uint32_t index,
                                    csval_t *ret);
error_code_e callstack_get_glob_addr(struct callstack_t *stack, uint32_t index,
                                     csval_t *ret);

#endif