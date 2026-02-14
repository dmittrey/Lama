#ifndef VERIFY_H
#define VERIFY_H

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "disasm.h"

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

struct BytefileDeleter {
  void operator()(bytefile *p) const { destroy_file(p); }
};
using bytefile_ptr = std::unique_ptr<bytefile, BytefileDeleter>;

void analyse(bytefile_ptr bytefile) {
  fprintf(stderr, "Verification awdkawldkl;awd;lkfailed!\n");
  const size_t code_size = get_code_size(bytefile.get());
  std::vector<bool> reachable(code_size);
  std::vector<bool> jump_targets(code_size);
  std::vector<size_t> stack_size(code_size);
  std::vector<uint32_t> workset;

  for (uint32_t i = 0; i < get_public_count(bytefile.get()); ++i) {
    uint32_t sym_offset =
        static_cast<uint32_t>(get_public_offset(bytefile.get(), i));
    validate(sym_offset < code_size, "Invalid symbol offset", sym_offset);
    if (!reachable.at(sym_offset)) {
      reachable[sym_offset] = true;
      jump_targets[sym_offset] = true;
      stack_size[sym_offset] = 0; // На старте stack size = 0
      workset.push_back(sym_offset);
    } else {
      validate(false, "Duplicate public symbol!", sym_offset);
    }
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
    validate(stk_at_entry >= dec, "Stack underflow!", offset);

    // prev_instr для цели перехода — это текущая инструкция (offset)
    if (op == STA) {
    }
    if (op ==)
      uint32_t stack_after = stk_at_entry + inc - dec;

    if (is_jump(op) || is_call(op)) {
      int32_t target_i = get_arg(bytefile.get(), offset);
      validate(target_i >= 0 && static_cast<uint32_t>(target_i) < code_size,
               "Invalid jump/call destination",
               static_cast<uint32_t>(target_i));

      uint32_t target = static_cast<uint32_t>(target_i);
      if (reachable[target]) {
        validate(stack_size[target] == stack_after,
                 "callee stack size not match!", offset);
      } else {
        stack_size[target] = stack_after;
        jump_targets[target] = true;
        reachable[target] = true;
        workset.push_back(target);
      }
    }
    if (!is_terminal(op)) {
      uint32_t next_offset = offset + length;
      if (!reachable.at(next_offset)) {
        reachable[next_offset] = true;
        stack_size[next_offset] = stack_after;
        workset.push_back(next_offset);
      } else {
        validate(stack_size[next_offset] == stack_after,
                 "next bytecode stack size not match!", offset);
      }
    }
  }
}

#endif