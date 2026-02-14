#ifndef VERIFY_H
#define VERIFY_H

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "disasm.h"

using stack_top3_t = std::array<int, 3>;

static inline stack_top3_t lub_stack_tops(const stack_top3_t &a,
                                          const stack_top3_t &b) {
  stack_top3_t r;
  for (size_t i = 0; i < 3; ++i)
    r[i] = (a[i] == b[i]) ? a[i] : (int)OT_UNKNOWN;
  return r;
}

static inline bool stack_tops_eq(const stack_top3_t &a, const stack_top3_t &b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static inline stack_top3_t
stack_after_instruction(stack_top3_t entry, uint32_t dec, int push_type) {
  int t0 = entry[0], t1 = entry[1], t2 = entry[2];
  if (dec == 0)
    return {push_type, t0, t1};
  if (dec == 1)
    return {push_type, t0, t1};
  if (dec == 2)
    return {push_type, t1, (int)OT_UNKNOWN};
  return {push_type, (int)OT_UNKNOWN, (int)OT_UNKNOWN};
}

namespace {
FILE *null_stream() {
  static FILE *f = nullptr;
  if (!f) {
    f = fopen("/dev/null", "w");
    if (f)
      setvbuf(f, nullptr, _IONBF, 0);
  }
  return f ? f : stdout;
}
} // namespace

static void validate(bool condition, const std::string &message,
                     uint32_t bytecode_offset) {
  if (!condition) {
    throw std::runtime_error(std::to_string(bytecode_offset) + ": " + message);
  }
}

static inline bool is_jump(bytecode op) noexcept {
  return op == JMP || op == CJMPZ || op == CJMPNZ;
}

static inline bool is_call(bytecode op) noexcept {
  return op == CALL || op == CALLC;
}

static inline bool is_terminal(bytecode op) noexcept {
  return op == JMP || op == END || op == RET || op == FAIL || op == STOP;
}

static inline int32_t get_second_arg(const bytefile *bf, int pos) {
  return *reinterpret_cast<const int32_t *>(bf->code_ptr + pos + 5);
}

struct BytefileDeleter {
  void operator()(bytefile *p) const { destroy_file(p); }
};
using bytefile_ptr = std::unique_ptr<bytefile, BytefileDeleter>;

void analyse(bytefile_ptr bytefile) {
  const size_t code_size = get_code_size(bytefile.get());
  std::vector<bool> reachable(code_size);
  std::vector<bool> jump_targets(code_size);
  std::vector<size_t> stack_size(code_size);
  std::vector<stack_top3_t> stack_tops(
      code_size,
      stack_top3_t{{(int)OT_UNKNOWN, (int)OT_UNKNOWN, (int)OT_UNKNOWN}});
  std::vector<uint32_t> workset;

  for (uint32_t i = 0; i < get_public_count(bytefile.get()); ++i) {
    uint32_t sym_offset =
        static_cast<uint32_t>(get_public_offset(bytefile.get(), i));
    validate(sym_offset < code_size, "Invalid symbol offset", sym_offset);
    if (!reachable.at(sym_offset)) {
      reachable[sym_offset] = true;
      jump_targets[sym_offset] = true;
      stack_size[sym_offset] = 0;
      workset.push_back(sym_offset);
    } else {
      validate(false, "Duplicate public symbol!", sym_offset);
    }
  }

  /* Build frame_at (nargs, nlocals) per offset and max_captures. */
  using frame_t = std::pair<uint32_t, uint32_t>;
  const uint32_t no_frame = static_cast<uint32_t>(-1);
  std::vector<frame_t> frame_at(code_size, {no_frame, no_frame});
  uint32_t max_captures = 0;
  uint32_t scan_pos = 0;
  while (scan_pos < code_size) {
    bytecode op;
    uint32_t inc = 0, dec = 0;
    int len_ret = disassemble_instruction(null_stream(), bytefile.get(),
                                         static_cast<int>(scan_pos), &op, &inc, &dec);
    validate(len_ret > 0, "Invalid instruction length", scan_pos);
    uint32_t len = static_cast<uint32_t>(len_ret);
    if (op == CLOSURE) {
      int32_t n = get_second_arg(bytefile.get(), static_cast<int>(scan_pos));
      if (n > 0 && static_cast<uint32_t>(n) > max_captures)
        max_captures = static_cast<uint32_t>(n);
    }
    if (op == BEGIN || op == CBEGIN) {
      int nest = 1;
      uint32_t p = scan_pos + len;
      uint32_t end_off = 0;
      bool found = false;
      while (p < code_size && nest > 0) {
        bytecode op2;
        int len2 = disassemble_instruction(null_stream(), bytefile.get(),
                                           static_cast<int>(p), &op2, nullptr,
                                           nullptr);
        validate(len2 > 0, "Invalid instruction in block", p);
        if (op2 == BEGIN || op2 == CBEGIN)
          nest++;
        else if (op2 == END) {
          nest--;
          if (nest == 0) {
            end_off = p;
            found = true;
            break;
          }
        }
        p += static_cast<uint32_t>(len2);
      }
      validate(found, "BEGIN/CBEGIN without matching END", scan_pos);
      int32_t w0 = get_arg(bytefile.get(), static_cast<int>(scan_pos));
      int32_t w1 = get_second_arg(bytefile.get(), static_cast<int>(scan_pos));
      uint32_t nargs = static_cast<uint32_t>(w0) & 0xFFFF;
      uint32_t nlocals = static_cast<uint32_t>(w1) & 0xFFFF;
      for (uint32_t i = scan_pos + len; i <= end_off && i < code_size;) {
        frame_at[i] = {nargs, nlocals};
        bytecode op_i;
        int len_i = disassemble_instruction(null_stream(), bytefile.get(),
                                            static_cast<int>(i), &op_i, nullptr,
                                            nullptr);
        validate(len_i > 0, "Invalid instruction in block", i);
        i += static_cast<uint32_t>(len_i);
      }
    }
    scan_pos += len;
  }

  while (!workset.empty()) {
    uint32_t offset = workset.back();
    workset.pop_back();

    bytecode op;
    uint32_t inc = 0;
    uint32_t dec = 0;
    size_t stk_at_entry = stack_size[offset];
    int len_ret =
        disassemble_instruction(null_stream(), bytefile.get(),
                                static_cast<int>(offset), &op, &inc, &dec);
    validate(len_ret > 0, "Invalid instruction length", offset);
    uint32_t length = static_cast<uint32_t>(len_ret);
    validate(offset + length <= code_size, "Unexpected end of code",
             offset + length);
    if (op == STA) {
      int second = stack_tops[offset][1];
      dec = (second == (int)OT_REF) ? 2 : 3;
      inc = 1;
    }
    validate(stk_at_entry >= dec, "Stack underflow!", offset);

    /* Index bounds (verified so interpreter can skip runtime checks). */
    if (op == LD_LOCAL || op == LDA_LOCAL || op == ST_LOCAL) {
      int32_t idx = get_arg(bytefile.get(), offset);
      validate(idx >= 0, "local index negative", offset);
      if (frame_at[offset].first != no_frame)
        validate(static_cast<uint32_t>(idx) < frame_at[offset].second,
                 "local index out of range", offset);
    } else if (op == LD_ARGUMENT || op == LDA_ARGUMENT || op == ST_ARGUMENT) {
      int32_t idx = get_arg(bytefile.get(), offset);
      validate(idx >= 0, "argument index negative", offset);
      if (frame_at[offset].first != no_frame)
        validate(static_cast<uint32_t>(idx) < frame_at[offset].first,
                 "argument index out of range", offset);
    } else if (op == LD_GLOBAL || op == LDA_GLOBAL || op == ST_GLOBAL) {
      int32_t idx = get_arg(bytefile.get(), offset);
      validate(idx >= 0, "global index negative", offset);
      validate(static_cast<uint32_t>(idx) <
                   get_global_area_size(bytefile.get()),
               "global index out of range", offset);
    } else if (op == LD_CAPTURED || op == LDA_CAPTURED || op == ST_CAPTURED) {
      int32_t idx = get_arg(bytefile.get(), offset);
      validate(idx >= 0, "captured index negative", offset);
      validate(static_cast<uint32_t>(idx) < max_captures,
               "captured index out of range", offset);
    }

    uint32_t stack_after = stk_at_entry + inc - dec;
    int push_type = get_pushed_operand_type(op);
    stack_top3_t new_tops =
        stack_after_instruction(stack_tops[offset], dec, push_type);

    if (is_jump(op) || is_call(op)) {
      int32_t target_i = get_arg(bytefile.get(), offset);
      size_t stack_at_target = stack_after;
      if (op == CALL) {
        int32_t nargs = *reinterpret_cast<const int32_t *>(
            bytefile.get()->code_ptr + offset + 5);
        validate(nargs >= 0, "CALL nargs negative", offset);
        stack_at_target = static_cast<size_t>(nargs);
      }
      if (op != CALLC) {
        validate(target_i >= 0 && static_cast<uint32_t>(target_i) < code_size,
                 "Invalid jump/call destination",
                 static_cast<uint32_t>(target_i));
        uint32_t target = static_cast<uint32_t>(target_i);
        if (reachable[target]) {
          if (stack_size[target] != stack_at_target) {
            std::string msg =
                "callee stack size not match! at offset " +
                std::to_string(offset) + " (target " + std::to_string(target) +
                "): expected " + std::to_string(stack_size[target]) + ", got " +
                std::to_string(stack_at_target);
            throw std::runtime_error(msg);
          }
          stack_top3_t merged = lub_stack_tops(stack_tops[target], new_tops);
          if (!stack_tops_eq(stack_tops[target], merged)) {
            stack_tops[target] = merged;
            workset.push_back(target);
          }
        } else {
          stack_size[target] = stack_at_target;
          stack_tops[target] = new_tops;
          jump_targets[target] = true;
          reachable[target] = true;
          workset.push_back(target);
        }
      }
    }
    if (!is_terminal(op)) {
      uint32_t next_offset = offset + length;
      if (!reachable.at(next_offset)) {
        reachable[next_offset] = true;
        stack_size[next_offset] = stack_after;
        stack_tops[next_offset] = new_tops;
        workset.push_back(next_offset);
      } else {
        if (stack_size[next_offset] != stack_after) {
          std::string msg = "next bytecode stack size not match! at offset " +
                            std::to_string(offset) + " (next " +
                            std::to_string(next_offset) + "): expected " +
                            std::to_string(stack_size[next_offset]) + ", got " +
                            std::to_string(stack_after);
          throw std::runtime_error(msg);
        }
        stack_top3_t merged = lub_stack_tops(stack_tops[next_offset], new_tops);
        if (!stack_tops_eq(stack_tops[next_offset], merged)) {
          stack_tops[next_offset] = merged;
          workset.push_back(next_offset);
        }
      }
    }
  }

  /* Second pass: for each BEGIN/CBEGIN compute max operand stack depth in block
     and write it into the high half of the first parameter. */
  uint32_t pos = 0;
  while (pos < code_size) {
    bytecode op;
    uint32_t inc = 0, dec = 0;
    int len_ret = disassemble_instruction(
        null_stream(), bytefile.get(), static_cast<int>(pos), &op, &inc, &dec);
    validate(len_ret > 0, "Invalid instruction length", pos);
    uint32_t len = static_cast<uint32_t>(len_ret);

    if (op == BEGIN || op == CBEGIN) {
      int nest = 1;
      uint32_t p = pos + len;
      uint32_t end_off = 0;
      bool found = false;
      while (p < code_size && nest > 0) {
        bytecode op2;
        int len2 = disassemble_instruction(null_stream(), bytefile.get(),
                                           static_cast<int>(p), &op2, nullptr,
                                           nullptr);
        validate(len2 > 0, "Invalid instruction length in block", p);
        if (op2 == BEGIN || op2 == CBEGIN)
          nest++;
        else if (op2 == END) {
          nest--;
          if (nest == 0) {
            end_off = p;
            found = true;
            break;
          }
        }
        p += static_cast<uint32_t>(len2);
      }
      validate(found, "BEGIN/CBEGIN without matching END", pos);

      size_t max_depth = 0;
      for (uint32_t i = pos; i <= end_off && i < code_size;) {
        if (reachable[i] && stack_size[i] > max_depth)
          max_depth = stack_size[i];
        bytecode op_i;
        int len_i = disassemble_instruction(null_stream(), bytefile.get(),
                                            static_cast<int>(i), &op_i, nullptr,
                                            nullptr);
        validate(len_i > 0, "Invalid instruction in block", i);
        i += static_cast<uint32_t>(len_i);
      }
      int32_t word0 = get_arg(bytefile.get(), static_cast<int>(pos));
      uint32_t nargs = static_cast<uint32_t>(word0) & 0xFFFF;
      uint32_t new_word0 = (static_cast<uint32_t>(max_depth) << 16) | nargs;
      int32_t *first_word =
          reinterpret_cast<int32_t *>(get_code_ptr(bytefile.get()) + pos + 1);
      *first_word = static_cast<int32_t>(new_word0);
    }

    pos += len;
  }
}

#endif