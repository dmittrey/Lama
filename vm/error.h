#ifndef ERROR_H
#define ERROR_H

typedef enum error_code_e {
  ERROR_NONE = 0,
  ERROR_STOP = 1,
  ERROR_GLOB_IDX_NEGATIVE = 2,
  ERROR_GLOB_IDX_OUT_OF_RANGE = 3,
  ERROR_STACK_UNDERFLOW = 4,
  ERROR_STACK_OVERFLOW = 5,
  ERROR_LOCL_IDX_NEGATIVE = 6,
  ERROR_LOCL_IDX_OUT_OF_RANGE = 7,
  ERROR_ARG_IDX_NEGATIVE = 8,
  ERROR_ARG_IDX_OUT_OF_RANGE = 9,
  ERROR_OPND_STACK_UNDERFLOW = 10,
  ERROR_NOT_ENOUGH_MEMORY = 11,
  ERROR_NOT_UNBOXED = 12,
  ERROR_NOT_BOXED = 13,
  ERROR_NOT_VALID_CLOSURE = 14,
} error_code_e;

#define RETURN_IF_ERROR(expr)                                                  \
  do {                                                                         \
    error_code_e _err = (expr);                                                \
    if (_err != ERROR_NONE) {                                                  \
      return _err;                                                             \
    }                                                                          \
  } while (0)

#endif