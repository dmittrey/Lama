#ifndef VERIFY_H
#define VERIFY_H

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "disasm.h"

typedef enum vtype_e { UNKNOWN = 0, REF, IMM, NDEF } vtype_e;

static void validate(bool condition, const std::string &message,
                     uint32_t bytecode_offset) {
  if (!condition) {
    fprintf(stderr, "0x%x: %s", bytecode_offset, message.c_str());
    throw std::runtime_error("");
  }
}

static inline bool is_jump(bytecode op) noexcept {
  return op == JMP || op == CJMPZ || op == CJMPNZ;
}

static inline bool is_call(bytecode op) noexcept { return op == CALL; }

static inline bool is_closure(bytecode op) noexcept { return op == CLOSURE; }

static inline bool is_callc(bytecode op) noexcept { return op == CALLC; }

static inline bool is_terminal(bytecode op) noexcept {
  return op == JMP || op == END || op == RET || op == FAIL || op == STOP;
}

static inline vtype_e join_type(vtype_e a, vtype_e b) noexcept {
  if (a == NDEF)
    return b;
  if (b == NDEF)
    return a;
  if (a == b)
    return a;
  return UNKNOWN;
}

static inline bool
update_types(std::pair<vtype_e, vtype_e> &dst,
             const std::pair<vtype_e, vtype_e> &src) noexcept {
  auto old = dst;
  dst.first = join_type(dst.first, src.first);
  dst.second = join_type(dst.second, src.second);
  return dst != old;
}

// Join rule for (stack_size, delta):
// require equal total depth; keep larger delta (more conservative).
static inline bool join_depth(uint16_t &dst_stk, uint32_t &dst_delta,
                              uint16_t in_stk, uint32_t in_delta,
                              uint32_t where) {
  size_t dst_total = dst_stk + (size_t)dst_delta;
  size_t in_total = in_stk + (size_t)in_delta;
  validate(dst_total == in_total,
           "Bytecode stack size not match with prev in same basic block!",
           where);

  uint32_t new_delta = (dst_delta > in_delta) ? dst_delta : in_delta;
  size_t new_stk = dst_total - (size_t)new_delta;

  bool changed = (new_delta != dst_delta) || (new_stk != dst_stk);
  dst_delta = new_delta;
  dst_stk = new_stk;
  return changed;
}

static inline std::pair<vtype_e, vtype_e>
compute_out_types(bytecode op, const std::pair<vtype_e, vtype_e> &in_types) {
  vtype_e top = in_types.first;
  vtype_e sec = in_types.second;

  auto norm = [](vtype_e t) -> vtype_e { return (t == NDEF) ? UNKNOWN : t; };
  top = norm(top);
  sec = norm(sec);

  switch (op) {
  case DROP:
    // new top <- old second
    // new second <- unknown
    return {sec, UNKNOWN};
  case SWAP:
    // new top <- old second
    // new second <- old top
    return {sec, top};
  case DUP:
    // new top <- old top
    // new sec <- old top
    return {top, top};

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
    // new sec <- old top
    return {IMM, top};

  // STI/STA -> +UNKNOWN
  case STI:
  case STA:
    // new top <- UNKNOWN
    // new sec <- old top
    return {UNKNOWN, top};

  // END/RET -> +top value type
  case END:
  case RET:
    return {top, sec};

  // CALL, CALLC, ELEM, LD, ST, LDA -> +UNKNOWN
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
    // new sec <- old top
    return {UNKNOWN, top};

  // +REF
  case CALL_LSTRING:
  case CLOSURE:
  case SEXP:
  case CALL_BARRAY:
    // new top <- REF
    // new sec <- old top
    return {REF, top};

  default:
    // do not model other ops
    return {UNKNOWN, UNKNOWN};
  }
}

struct BytefileDeleter {
  void operator()(bytefile *p) const { destroy_file(p); }
};
using bytefile_ptr = std::unique_ptr<bytefile, BytefileDeleter>;

int analyse(bytefile_ptr bytefile) {
  const size_t code_size = get_code_size(bytefile.get());
  std::vector<bool> reachable(code_size, false);             // 1/8X file size
  std::vector<uint16_t> stack_size(code_size, 0);            // 2X file size
  std::vector<uint32_t> delta_size(code_size, 0);            // 4X file size
  std::vector<uint32_t> workset;                             // 4X file size
  std::vector<std::pair<vtype_e, vtype_e>> types(code_size); //(top, second)

  // O(n), n - public symbols
  for (uint32_t i = 0; i < get_public_count(bytefile.get()); ++i) {
    uint32_t sym_offset =
        static_cast<uint32_t>(get_public_offset(bytefile.get(), i));
    validate(sym_offset < code_size, "Invalid symbol offset!", sym_offset);
    if (!reachable.at(sym_offset)) {
      reachable[sym_offset] = true;
      stack_size[sym_offset] = 0; // На старте stack size = 0
      delta_size[sym_offset] = 0;
      types[sym_offset] = {NDEF, NDEF};
      workset.push_back(sym_offset);
    } else {
      validate(false, "Duplicate public symbol!", sym_offset);
    }
  }

  // O(r), r - amount of reachable symbols
  while (!workset.empty()) {
    // Take from workset
    uint32_t offset = workset.back();
    workset.pop_back();

    // Take info of current bytecode
    bytecode op;
    uint32_t inc = 0; // Increment stack *inc* times
    uint32_t dec = 0; // Decrement stack *dec* times
    size_t stk_at_entry = stack_size[offset];
    uint32_t delta_entry = delta_size[offset];

    int len_ret = disassemble_instruction(
        stdin, bytefile.get(), static_cast<int>(offset), &op, &inc, &dec);
    validate(len_ret > 0, "Invalid instruction length", offset);

    if (op == CBEGIN || op == BEGIN) {
      // fprintf(stderr, "%x: %hu\n", offset, stack_size[offset]);
      set_arg2_bighalf(bytefile.get(), offset, stack_size[offset]);
    }

    // Bounds in code segment
    uint32_t length = static_cast<uint32_t>(len_ret);
    validate(offset + length <= code_size, "Unexpected end of code",
             offset + length);

    validate(stk_at_entry + (size_t)delta_entry >= (size_t)dec,
             "Stack underflow!", offset);

    uint32_t stk_at_next = (uint32_t)(stk_at_entry + inc - dec);
    uint32_t delta_at_next = delta_entry;

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
    минимальному stack_size среди всех closure, но это такое
      */

    std::pair<vtype_e, vtype_e> in_types = types[offset];
    std::pair<vtype_e, vtype_e> out_types = compute_out_types(op, in_types);

    if (op == LD_GLOBAL || op == LDA_GLOBAL || op == ST_GLOBAL) {
      int32_t idx = get_arg(bytefile.get(), offset);
      validate(idx >= 0 && idx < bytefile.get()->global_area_size,
               "Global index out of bounds", offset);
    }
    if (op == STRING) {
      int32_t idx = get_arg(bytefile.get(), offset);
      validate(idx >= 0 && idx < bytefile.get()->stringtab_size,
               "STRING index out of string table bounds", offset);
    }
    if (op == STI) {
      vtype_e t_sec = types[offset].second;

      // second-to-top must be REF
      validate(t_sec != IMM, "STI type mismatch: expected REF below value",
               offset);
      // refine UNKNOWN/NDEF -> REF
      if (t_sec == UNKNOWN || t_sec == NDEF) {
        types[offset].second = REF;
      }
    }

    if (op == STA) {
      vtype_e t_sec = types[offset].second;

      if (t_sec == REF) {
        // STI-like: pop2 push1 => net -1
        validate(stk_at_entry + (size_t)delta_entry >= 2,
                 "STA (STI-like) stack underflow!", offset);
        stk_at_next = (uint32_t)(stk_at_entry - 1);
      } else if (t_sec == IMM) {
        // indexed: pop3 push1 => net -2
        validate(stk_at_entry + (size_t)delta_entry >= 3,
                 "STA (indexed) stack underflow!", offset);
        stk_at_next = (uint32_t)(stk_at_entry - 2);
      } else if (t_sec == UNKNOWN || t_sec == NDEF) {
        // Unknown second-to-top: treat as IMM (index) to keep CFG stack depths
        // consistent
        validate(stk_at_entry + (size_t)delta_entry >= 3,
                 "STA (indexed) stack underflow!", offset);
        // refine UNKNOWN/NDEF -> IMM
        types[offset].second = IMM;
        // indexed: pop3 push1 => net -2
        stk_at_next = (uint32_t)(stk_at_entry - 2);
        // delta unchanged
      } else {
        validate(false, "STA internal type state error", offset);
      }
    }

    // Validate closure destination bounds (no CFG edge)
    if (is_closure(op)) {
      int32_t target_i = get_arg(bytefile.get(), offset);
      uint32_t target = static_cast<uint32_t>(target_i);
      validate(target_i >= 0 && static_cast<size_t>(target_i) < code_size,
               "Invalid closure destination", offset);
    }

    if (is_jump(op) || is_call(op)) {
      int32_t target_i = get_arg(bytefile.get(), offset);
      uint32_t target = static_cast<uint32_t>(target_i);
      validate(target_i >= 0 && static_cast<size_t>(target_i) < code_size,
               "Invalid jump/call destination", offset);
      size_t expected_at_target =
          is_call(op) ? static_cast<size_t>(dec) : stk_at_next;

      if (!reachable.at(target)) {
        reachable[target] = true;
        stack_size[target] = expected_at_target;
        delta_size[target] = delta_at_next;
        if (types[target].first == NDEF && types[target].second == NDEF)
          types[target] = {UNKNOWN, UNKNOWN};
        workset.push_back(target);
      } else {
        if (is_call(op)) {
          // CALL targets must be exact and delta-free
          validate(delta_size[target] == 0 && delta_at_next == 0,
                   "CALL target delta mismatch!", target);
          validate(stack_size[target] == expected_at_target,
                   "Callee stack size not match with caller!", target);
        } else {
          bool changed = join_depth(stack_size[target], delta_size[target],
                                    expected_at_target, delta_at_next, target);
          if (changed)
            workset.push_back(target);
        }
      }
    }

    // Check stk size propagation in basic block
    if (!is_terminal(op)) {
      uint32_t next_offset = offset + length;
      if (!reachable.at(next_offset)) {
        reachable[next_offset] = true;
        stack_size[next_offset] = (size_t)stk_at_next;
        delta_size[next_offset] = delta_at_next;
        types[next_offset] = out_types;
        workset.push_back(next_offset);
      } else {
        bool depth_changed =
            join_depth(stack_size[next_offset], delta_size[next_offset],
                       (size_t)stk_at_next, delta_at_next, next_offset);
        bool type_changed = update_types(types[next_offset], out_types);

        if (depth_changed || type_changed) {
          workset.push_back(next_offset);
        }
      }
    }
  }

  return 0;
}

#endif