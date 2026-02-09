#pragma once

#include <memory>
#include <string>
#include <vector>

#include "bytefile.h"
#include "disasm.h"

class BytecodeFreq {
public:
  explicit BytecodeFreq(const char *const fname);

  void analyse();

private:
  struct BytefileDeleter {
    void operator()(bytefile *p) const { destroy_file(p); }
  };
  using bytefile_ptr = std::unique_ptr<bytefile, BytefileDeleter>;
  bytefile_ptr bytefile_;

private:
  void find_reachable_instructions();
  void find_idioms_single();
  void find_idioms_double();

private:
  std::vector<char> reachable_;    // 1X file's code section size
  std::vector<char> jump_targets_; // 1X file's code section size
  std::vector<std::pair<uint32_t, uint32_t>>
      Idioms_; // 8X file's code section size
               // Summary 10X file size (6X left)

private:
  static bool is_jump(bytecode op) noexcept;
  static bool is_call(bytecode op) noexcept;
  static bool is_terminal(bytecode op) noexcept;
};