#ifndef ERROR_H
#define ERROR_H

typedef enum error_code_e {
  ERROR_NONE = 0,
  ERROR_STOP = 1,
  ERROR_GLOB_IDX_NEGATIVE = 2,
  ERROR_GLOB_IDX_OUT_OF_RANGE = 3,
} error_code_e;

#endif