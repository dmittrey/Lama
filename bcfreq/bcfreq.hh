#pragma once

#include <memory>
#include <string>
#include <vector>

#include "bytefile.h"
#include "disasm.h"

class BytecodeFreq final {
public:
  explicit BytecodeFreq(const char *const fname);
  ~BytecodeFreq();

public:
  void analyse();

private:
  struct BytefileDeleter {
    void operator()(bytefile *p) const { destroy_file(p); }
  };
  using bytefile_ptr = std::unique_ptr<bytefile, BytefileDeleter>;
  bytefile_ptr bytefile_;

private:
  void find_reachable_instructions(std::vector<bool> &reachable,
                                   std::vector<bool> &jump_targets);
  void find_idioms_single(std::vector<std::pair<uint32_t, uint32_t>> &v,
                          const std::vector<bool> &reachable,
                          const std::vector<bool> &jump_targets);
  void find_idioms_double(
      std::vector<std::pair<uint32_t, uint32_t>> &v,
      const std::vector<std::pair<uint32_t, uint32_t>> &single_idioms);

private:
  static bool is_jump(bytecode op) noexcept;
  static bool is_call(bytecode op) noexcept;
  static bool is_terminal(bytecode op) noexcept;
};