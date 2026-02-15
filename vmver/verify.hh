#ifndef VERIFY_H
#define VERIFY_H

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "disasm.h"

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

static inline bool is_terminal(bytecode op) noexcept {
  return op == JMP || op == END || op == RET || op == FAIL || op == STOP;
}

struct BytefileDeleter {
  void operator()(bytefile *p) const { destroy_file(p); }
};
using bytefile_ptr = std::unique_ptr<bytefile, BytefileDeleter>;

int analyse(bytefile_ptr bytefile) {
  // fprintf(stderr, "Verification!\n");
  const size_t code_size = get_code_size(bytefile.get());
  std::vector<bool> reachable(code_size, false);    // 1/8X file size
  std::vector<bool> jump_targets(code_size, false); // 1/8X file size
  std::vector<size_t> stack_size(code_size, 0);     // 8X file size
  std::vector<uint32_t> workset;                    // 4X file size
  // Summary 12.25X file size

  // O(n), n - public symbols
  for (uint32_t i = 0; i < get_public_count(bytefile.get()); ++i) {
    uint32_t sym_offset =
        static_cast<uint32_t>(get_public_offset(bytefile.get(), i));
    validate(sym_offset < code_size, "Invalid symbol offset!", sym_offset);
    if (!reachable.at(sym_offset)) {
      reachable[sym_offset] = true;
      jump_targets[sym_offset] = true;
      stack_size[sym_offset] = 0; // На старте stack size = 0
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
    int len_ret = disassemble_instruction(
        stdin, bytefile.get(), static_cast<int>(offset), &op, &inc, &dec);
    validate(len_ret > 0, "Invalid instruction length", offset);
    validate(stk_at_entry >= dec, "Stack underflow!", offset);

    // Validate bytecode position
    uint32_t length = static_cast<uint32_t>(len_ret);
    validate(offset + length <= code_size, "Unexpected end of code",
             offset + length);

    //  Calc stack size for next bytecode
    uint32_t stk_at_next = stk_at_entry + inc - dec;

    if (is_jump(op) || is_call(op)) {
      int32_t target_i = get_arg(bytefile.get(), offset);
      uint32_t target = static_cast<uint32_t>(target_i);
      validate(target_i >= 0 && static_cast<size_t>(target_i) < code_size,
               "Invalid jump/call destination", offset);
      size_t expected_at_target =
          is_call(op) ? static_cast<size_t>(dec) : stk_at_next;

      if (!reachable.at(target)) {
        stack_size[target] = expected_at_target;
        reachable[target] = true;
        workset.push_back(target);
      } else {
        validate(stack_size[target] == expected_at_target,
                 "Callee stack size not match with caller!", target);
      }
    }

    // Check stk size propagation in basic block
    if (!is_terminal(op)) {
      uint32_t next_offset = offset + length;
      if (!reachable.at(next_offset)) {
        stack_size[next_offset] = stk_at_next;
        reachable[next_offset] = true;
        workset.push_back(next_offset);
      } else {
        validate(stack_size[next_offset] == stk_at_next,
                 "Bytecode stack size not match with prev in same basic block!",
                 next_offset);
      }
    }
  }
  // fprintf(stderr, "Verified!\n");
  return 0;
}

#endif