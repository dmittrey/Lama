#pragma once

#include <map>
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
  void find_idioms();
  void fill_idioms();

private:
  std::vector<char> reachable_;    // 1X file's code section size
  std::vector<char> jump_targets_; // 1X file's code section size
  std::vector<char> OnesIdioms_;   // 1X file's code section size (Mark as 1
                                   // pos equals to idiom pos)
  std::vector<char> TwosIdioms_;   // 1X file's code section size (Mark as 1
                                   // pos equals to idiom pos)
  std::map<bytecode,
           size_t>     // Amount of entities in bytecode enum (72 in cur model *
      OnesIdiomsFreq_; // (sizeof(bytecode) + sizeof(size_t)) Const memory size
  std::map<std::pair<bytecode, bytecode>,
           size_t> // Amount of entities in bytecode enum (72*72 in cur model *
      TwoIdiomsFreq_; // (sizeof(bytecode) + sizeof(size_t)) Const memory size

private:
  static bool is_jump(bytecode op) noexcept;
  static bool is_call(bytecode op) noexcept;
  static bool is_terminal(bytecode op) noexcept;
};