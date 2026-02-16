#ifndef VERIFY_H
#define VERIFY_H

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "disasm.h"

// Code symbol offset
// Operate with 32bit words => 32 bit width
typedef uint32_t symoff;

static const symoff FUNC_UNDEF = (symoff)~0u;

/* STA/STI
 Хочу попробовать следующим образом решить проблему: пусть будет
 * решетка UNKNOWN(top)
 *             / \
 *           REF IMM
 *             \ /
 *             NDEF
 * Я предполагаю что мы будем
 * двигаться только вверх, конкретизация UNKNOWN -> REF как наименее
 * консервативный вариант для STA, то есть меньше возьмем со стека, меньше
 * возможность underflow
 *
 * Хочу держать для каждого offset пару двух верхних элементов стека, в
 таком
 * случае если я буду видеть что у меня
 * - точно REF, то снимаю 2 ложу 1.
 * - точно IMM, снимаю 3 ложу 1.
 * - Unknown -> REF снимаю 2 ложу 1.
 *
 * Но есть проблема что у меня возможно размеры стеков не совпадут, можно
 * ввести дельту, если вижу UNKNOWN, то добавить 1 к дельте для точки
 которую
 * буду добавлять в workset */
typedef enum vtype_e { UNKNOWN = 0, REF, IMM, NDEF } vtype_e;
// Stack dump(top, sec)
#pragma pack(push, 1)
typedef struct stdump {
  vtype_e top;
  vtype_e sec;
} stdump;
#pragma pack(pop)

static inline bool is_jump(bytecode op) {
  return op == JMP || op == CJMPZ || op == CJMPNZ;
}

static inline bool is_terminal(bytecode op) {
  return op == JMP || op == END || op == RET || op == FAIL || op == STOP;
}

static inline vtype_e join_type(vtype_e a, vtype_e b) {
  if (a == NDEF)
    return b;
  if (b == NDEF)
    return a;
  if (a == b)
    return a;
  return UNKNOWN;
}

// merge astate
static inline bool update_types(stdump *const dst, stdump src) {
  const vtype_e old_dst_top = dst->top;
  const vtype_e old_dst_sec = dst->sec;
  dst->top = join_type(dst->top, src.top);
  dst->sec = join_type(dst->sec, src.sec);
  return old_dst_top != dst->top || old_dst_sec != dst->sec;
}

static inline int join_depth(sdepth *const dst_stk, sdepth *const dst_delta,
                             sdepth in_stk, sdepth in_delta, symoff offset,
                             bool *const changed) {
  sdepth dst_total = *dst_stk + *dst_delta;
  sdepth in_total = in_stk + in_delta;
  if (dst_total != in_total) {
    fprintf(stderr, "%x: Stack depth not match in pathes join! (%d != %d)",
            offset, dst_total, in_total);
    return 1;
  }

  // Возьмем менее консервативную оценка тк при join получаем додлжны получить
  // менее конкретный результат
  sdepth new_delta = (*dst_delta > in_delta) ? *dst_delta : in_delta;
  sdepth new_stk = dst_total - new_delta;

  *changed = (new_delta != *dst_delta) || (new_stk != *dst_stk);
  *dst_delta = new_delta;
  *dst_stk = new_stk;
  return 0;
}

static inline void compute_out_types(bytecode op, stdump in,
                                     stdump *const out) {
  // TODO а надо ли нормализовывать?
  in.top = (in.top == NDEF) ? UNKNOWN : in.top;
  in.sec = (in.sec == NDEF) ? UNKNOWN : in.sec;

  switch (op) {
  case DROP:
    // new top <- old second
    out->top = in.sec;
    // new second <- unknown
    out->sec = UNKNOWN;
    break;
  case SWAP:
    // new top <- old second
    out->top = in.sec;
    // new second <- old top
    out->sec = in.top;
    break;
  case DUP:
    // new top <- old top
    out->top = in.top;
    // new sec <- old top
    out->sec = in.top;
    break;

  // +IMM
  case LOW_ADD:
  case LOW_SUB:
  case LOW_MUL:
  case LOW_DIV:
  case LOW_MOD:
  case LOW_LT:
  case LOW_LE:
  case LOW_GT:
  case LOW_GE:
  case LOW_EQ:
  case LOW_NE:
  case LOW_AND:
  case LOW_OR:
  case CONST:
  case STRING:
  case TAG:
  case ARRAY:
  case PATT_STR:
  case PATT_STRING:
  case PATT_ARRAY:
  case PATT_SEXP:
  case PATT_REF:
  case PATT_VAL:
  case PATT_FUN:
  case CALL_LREAD:
  case CALL_LWRITE:
  case CALL_LLENGTH:
    // new top <- IMM
    out->top = IMM;
    // new sec <- old top
    out->sec = in.top;
    break;

  // +REF
  case CLOSURE:
  case SEXP:
  case CALL_BARRAY:
  case CALL_LSTRING:
    // new top <- REF
    out->top = REF;
    // new sec <- old top
    out->sec = in.top;
    break;

  // +UNKNOWN
  case STI:
  case STA:
  case CALL:
  case CALLC:
  case ELEM:
  case LD_GLOBAL:
  case LD_LOCAL:
  case LD_ARGUMENT:
  case LD_CAPTURED:
  case ST_GLOBAL:
  case ST_LOCAL:
  case ST_ARGUMENT:
  case ST_CAPTURED:
  case LDA_GLOBAL:
  case LDA_LOCAL:
  case LDA_ARGUMENT:
  case LDA_CAPTURED:
    // new top <- UNKNOWN
    out->top = UNKNOWN;
    // new sec <- old top
    out->sec = in.top;
    break;

  // -top
  case JMP:
  case CJMPZ:
  case CJMPNZ:
    // new top <- old sec
    out->top = in.sec;
    // new sec <- UNKNOWN
    out->sec = UNKNOWN;
    break;

  // not changing stack
  default:
    // new top <- old top
    out->top = in.top;
    // new sec <- old sec
    out->sec = in.sec;
    break;
  }
}

static inline int write_sdep_at_entry(bytefile *bf, symoff f, sdepth sdep) {
  bytecode op;
  int len = disassemble_instruction(stdin, bf, (int)f, &op, NULL, NULL);
  if (len <= 0) {
    fprintf(stderr, "%x: Invalid entry instruction\n", f);
    return 1;
  }
  if (op != BEGIN && op != CBEGIN) {
    fprintf(stderr, "%x: Function entry is not BEGIN/CBEGIN\n", f);
    return 1;
  }
  set_arg2_bighalf(bf, (int)f, sdep);
  return 0;
}

static inline int propagate_func(symoff *func_of, symoff offset, symoff f) {
  if (f == FUNC_UNDEF) {
    fprintf(stderr, "%x: propagate_func with f=FUNC_UNDEF\n", offset);
    return 1;
  }
  if (func_of[offset] == FUNC_UNDEF) {
    func_of[offset] = f;
    return 0;
  }
  if (func_of[offset] != f) {
    fprintf(stderr, "%x: offset belongs to multiple functions (%x vs %x)\n",
            offset, func_of[offset], f);
    return 1;
  }
  return 0;
}

symoff *workset = NULL;
symoff *workset_head = NULL;
static inline bool __ws_empty() { return workset_head == (workset - 1); }
static inline void __ws_push(symoff sym) {
  memcpy(++workset_head, &sym, sizeof(symoff));
}
static inline symoff __ws_pop() { return *(workset_head--); }

int verify(bytefile *bytefile) {
  const size_t code_size = get_code_size(bytefile);
  bool *reachable = (bool *)calloc(code_size, sizeof(bool)); // 1X file size
  sdepth *stack_size =
      (sdepth *)calloc(code_size, sizeof(sdepth)); // 2X file size
  sdepth *max_depth =
      (sdepth *)calloc(code_size, sizeof(sdepth)); // 2X file size
  symoff *func_of =
      (symoff *)malloc(code_size * sizeof(symoff)); // 4X file size
  if (!func_of) {
    fprintf(stderr, "Not enough memory for func_of\n");
    return 1;
  }
  for (size_t i = 0; i < code_size; i++) {
    func_of[i] = FUNC_UNDEF;
  }
  sdepth *delta_size =
      (sdepth *)calloc(code_size, sizeof(sdepth)); // 2X file size
  stdump *types =
      (stdump *)calloc(code_size, sizeof(stdump)); // 2X file size(cause packed)
  workset = (symoff *)calloc(code_size, sizeof(symoff)); // 4X file size
  workset_head = workset - 1;

  // O(n), n - public symbols
  for (uint32_t i = 0; i < get_public_count(bytefile); ++i) {
    symoff sym_offset = get_public_offset(bytefile, i);
    if (sym_offset >= code_size) {
      fprintf(stderr, "%x: Invalid symbol offset!", sym_offset);
      return 1;
    }
    if (!reachable[sym_offset]) {
      reachable[sym_offset] = true;
      stack_size[sym_offset] = 0; // На старте stack size = 0
      delta_size[sym_offset] = 0;
      types[sym_offset] = (struct stdump){.top = NDEF, .sec = NDEF};
      __ws_push(sym_offset);
    } else {
      fprintf(stderr, "%x: Duplicate public symbol!", sym_offset);
    }
  }

  /*
  Из-за того что пользуемся монотонным фреймворком, то нам нужно через некоторые
  CFG пути пройти несколько раз чтобы по решетке дойти до элемента который не
  будет менятся на след итерации

  Предполагаю что на ревью могут возникнуть вопросы, связанные с выходом за
  отведенное пространство нашего workset работающего как очередь но размещенного
  как вектор в памяти под callstack.
  Соображения следующие: Из-за того что мы знаем что наш алгоритм положит в
  очередь ровно 1 элемент на 1 отнятый и также из-за того что мы знаем что
  изначально в нем может быть максимум(если говорим что все символы в code
  segment публичны) общее кол-во символов нашего code segment, то мы можем
  говорить что больше чем 4X файла наша очередь не займет(на каждый адресуемый
  байт храним его отступ и если положим все отступы) и также наш алгоритм будет
  работать за N * const, N - кол-во достижимых символов из публичных. А
  константа будет кол-во проходов чтобы заземлить AbsState.
  */
  while (!__ws_empty()) {
    // Take from workset
    symoff offset = __ws_pop();

    symoff f = func_of[offset];
    if (f == FUNC_UNDEF) {
      fprintf(stderr, "%x: reached code without function entry\n", offset);
      return 1;
    }

    // Take info of current bytecode
    bytecode op;
    sdepth inc = 0; // Increment stack *inc* times
    sdepth dec = 0; // Decrement stack *dec* times
    sdepth stk_at_entry = stack_size[offset];
    sdepth delta_entry = delta_size[offset];

    int length =
        disassemble_instruction(stdin, bytefile, offset, &op, &inc, &dec);
    if (length <= 0) {
      fprintf(stderr, "%x: Invalid instruction length", offset);
      return 1;
    }

    if (op == CBEGIN || op == BEGIN) {
      // fprintf(stderr, "%x: %hu\n", offset, stack_size[offset]);
      set_arg2_bighalf(bytefile, offset, stack_size[offset]);
    }

    // Bounds in code segment
    if (offset + length > code_size) {
      fprintf(stderr, "%x: Unexpected end of code!", offset + length);
      return 1;
    }
    if (stk_at_entry + delta_entry < dec) {
      fprintf(stderr, "%x: Stack underflow!", offset);
      return 1;
    }

    sdepth stk_at_next = stk_at_entry + inc - dec;
    sdepth delta_at_next = delta_entry;

    stdump out_types;
    compute_out_types(op, types[offset], &out_types);

    // check ERROR_GLOB_IDX_OUT_OF_RANGE
    if (op == LD_GLOBAL || op == LDA_GLOBAL || op == ST_GLOBAL) {
      int32_t idx = get_arg(bytefile, offset);
      if (idx < 0 || idx >= bytefile->global_area_size) {
        fprintf(stderr, "%x: Global index out of bounds!", offset);
        return 1;
      }
    }
    // check ERROR_STRING_IDX_OUT_OF_RANGE
    if (op == STRING) {
      int32_t idx = get_arg(bytefile, offset);
      if (idx < 0 || idx >= bytefile->stringtab_size) {
        fprintf(stderr, "%x: String index out of bounds!", offset);
        return 1;
      }
    }
    if (op == STI) {
      vtype_e t_sec = types[offset].sec;

      // second-to-top must be REF
      if (t_sec == IMM) {
        fprintf(stderr, "%x: STI type mismatch: expected REF below value",
                offset);
        return 1;
      }

      // refine UNKNOWN/NDEF -> REF
      if (t_sec == UNKNOWN || t_sec == NDEF) {
        types[offset].sec = REF;
      }
    }
    if (op == STA) {
      vtype_e t_sec = types[offset].sec;

      if (t_sec == REF) {
        // STI-like: pop2 push1 => net -1
        if (stk_at_entry + (size_t)delta_entry < 2) {
          fprintf(stderr, "%x: STA (STI-like) stack underflow!", offset);
          return 1;
        }
        // omitted in disassemble_instruction
        stk_at_next = stk_at_entry - 1;
      } else if (t_sec == IMM) {
        // indexed: pop3 push1 => net -2
        if (stk_at_entry + (size_t)delta_entry < 3) {
          fprintf(stderr, "%x: STA (indexed) stack underflow!", offset);
          return 1;
        }
        stk_at_next = stk_at_entry - 2;
      } else {
        // Unknown: предполагаем худшее и делаем уточнение в IMM, чтобы глубины
        // в любом случае хватило
        if (stk_at_entry + (size_t)delta_entry < 3) {
          fprintf(stderr, "%x: STA (indexed) stack underflow!", offset);
          return 1;
        }

        // refine UNKNOWN/NDEF -> IMM
        types[offset].sec = IMM;
        // indexed: pop3 push1 => net -2
        stk_at_next = (uint32_t)(stk_at_entry - 2);
      }
    }

    /*
    try#1
      Closure:
      1. У нас всегда будет хранится для каждой точки куда будет вести callc
      target
      2. Если мы видим closure то на след шаге изменим callc target на новый

      Callc:
      1. Если встретили callc, то нам нужно взять для текущей ноды callc_target
      и для него посчитать глубины как для is_jump
      2. Для следующей ноды поставить предыдущий callc target
    try#2
    Проблема того, что нужно держать очередь closures для конкретной ноды
    кажется критичной(ну то есть уходим от статики)

    Решил отказаться от моделирования стека замыканий и чекаю только bounds
    перехода
    +
    Для callc чекаем что глубина корректна для след ноды

    p.s. Возможно можно индексировать массив точек куда ведет хоть какая-то
    closure и при callc пробегаться и смотреть инвариант что наш stack_size >=
    минимальному stack_size среди всех closure. для этого нам нужно знать
    stack_size вообще всех closure, следовательно, нужно в два прохода
    анализировать.
    */
    if (op == CLOSURE) {
      int32_t target_i = get_arg(bytefile, offset);
      symoff target = (symoff)target_i;
      if (target_i < 0 || target >= code_size) {
        fprintf(stderr, "%x: Invalid closure destination!", offset);
        return 1;
      }

      int32_t nvars = get_arg2(bytefile, offset);
      if (nvars < 0) {
        fprintf(stderr, "%x: Closure nvars < 0", offset);
        return 1;
      }
    }
    if (is_jump(op) || op == CALL) {
      int32_t target_i = get_arg(bytefile, offset);
      symoff target = (symoff)target_i;
      if (target_i < 0 || target >= code_size) {
        fprintf(stderr, "%x: Invalid jump/call destination!", offset);
        return 1;
      }

      // Когда зайдем в CALL у нас глубина определяется кол-вом аргументов
      size_t expected_at_target = op == CALL ? dec : stk_at_next;

      if (op == CALL) {
        bytecode nop;
        int len_ret = disassemble_instruction(stdin, bytefile, target_i, &nop,
                                              NULL, NULL);
        if (nop != BEGIN) {
          fprintf(stderr, "%x: Call not transfer to begin/end section!",
                  offset);
          return 1;
        }

        int32_t begin_args_cnt = get_arg(bytefile, target_i);
        int32_t call_args_cnt = get_arg2(bytefile, offset);
        if (begin_args_cnt != call_args_cnt) {
          fprintf(stderr, "%x: Call and begin args count not match!", offset);
          return 1;
        }
      }

      if (!reachable[target]) {
        reachable[target] = true;
        stack_size[target] = expected_at_target;
        delta_size[target] = delta_at_next;
        if (types[target].top == NDEF && types[target].sec == NDEF)
          types[target] = (struct stdump){.top = UNKNOWN, .sec = UNKNOWN};
        __ws_push(target);
      } else {
        if (op == CALL) {
          // CALL targets must be exact and delta-free
          if (delta_size[target] != 0 || delta_at_next != 0) {
            fprintf(stderr, "%x: CALL target delta mismatch!", target);
            return 1;
          }
          if (stack_size[target] != expected_at_target) {
            fprintf(stderr, "%x: Callee stack size not match with caller!",
                    target);
          }
        }
        // Not reached jump
        else {
          bool changed;

          if (join_depth(&stack_size[target], &delta_size[target],
                         expected_at_target, delta_at_next, target, &changed)) {
            return 1;
          }

          if (changed)
            __ws_push(target);
        }
      }
    }

    // Check stk size propagation in basic block
    if (!is_terminal(op)) {
      uint32_t next_offset = offset + length;
      if (!reachable[next_offset]) {
        reachable[next_offset] = true;
        stack_size[next_offset] = (size_t)stk_at_next;
        delta_size[next_offset] = delta_at_next;
        types[next_offset] = out_types;
        __ws_push(next_offset);
      } else {
        bool depth_changed;
        if (join_depth(&stack_size[next_offset], &delta_size[next_offset],
                       (size_t)stk_at_next, delta_at_next, next_offset,
                       &depth_changed)) {
          return 1;
        }
        bool type_changed = update_types(&types[next_offset], out_types);

        if (depth_changed || type_changed)
          __ws_push(next_offset);
      }
    }
  }
  return 0;
}

#endif