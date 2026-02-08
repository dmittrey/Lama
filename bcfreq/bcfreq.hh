#pragma once

#include <memory>
#include <string>
#include <vector>

#include "bytefile.h"
#include "disasm.h"

class BytecodeFreq {
public:
  struct BytefileDeleter {
    void operator()(bytefile *p) const { destroy_file(p); }
  };
  using bytefile_ptr = std::unique_ptr<bytefile, BytefileDeleter>;

  explicit BytecodeFreq(const char *const fname);

  void find_reachable_instructions();

  void find_idioms();

  [[nodiscard]] const bytefile *get_bytefile() const noexcept {
    return bytefile_.get();
  }

private:
  bytefile_ptr bytefile_;
  std::vector<char> reachable_;    // Same as file's code section size
  std::vector<char> jump_targets_; // Same as file's code section size
  std::vector<char> OnesIdioms_; // Same as file's code section size (Mark as 1
                                 // pos equals to idiom pos)
  std::vector<char> TwosIdioms_; // Same as file's code section size (Mark as 1
                                 // pos equals to idiom pos)

  static bool is_jump(bytecode op) noexcept;

  static bool is_call(bytecode op) noexcept;

  static bool is_terminal(bytecode op) noexcept;
};