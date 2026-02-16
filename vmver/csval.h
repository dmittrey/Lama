#ifndef CSVAL_H
#define CSVAL_H

#include "error.h"
#include "runtime_common.h"

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

/* raw imm -> {.ty = CS_IMM, .val = BOX((aint)imm)} */
static inline csval_t csval_imm(int32_t imm) {
  return (csval_t){.ty = CS_IMM, .val = BOX((aint)imm)};
}
/* raw aint* ref -> {.ty = CS_EXTERNAL_REF, .val = (aint)ref} */
static inline csval_t csval_extern(aint *ref) {
  return (csval_t){.ty = CS_EXTERNAL_REF, .val = (aint)ref};
}
/* raw offset -> {.ty = CS_INTERNAL_REF, .val = BOX((aint)offset)} */
static inline csval_t csval_intern(uint32_t offset) {
  return (csval_t){.ty = CS_INTERNAL_REF, .val = BOX((aint)offset)};
}

/* Mapping */
static inline csval_t csval_from_aint(aint v) {
  return UNBOXED(v) ? csval_imm((uint32_t)UNBOX(v)) : csval_extern((aint *)v);
}
static inline aint csval_to_aint(csval_t val) { return val.val; }
static error_code_e csval_to_imm_aint(csval_t val, aint *ret) {
  if (val.ty != CS_IMM) {
    return ERROR_NOT_IMM;
  }
  *ret = val.val;
  return ERROR_NONE;
}

#endif