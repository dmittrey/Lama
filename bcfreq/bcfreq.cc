#include <algorithm>
#include <iostream>
#include <sstream>

#include "bcfreq.hh"

static void validate(bool condition, const std::string &message,
                     uint32_t bytecode_offset) {
  if (!condition) {
    throw new std::runtime_error(std::to_string(bytecode_offset) + ": " +
                                 message);
  }
}

BytecodeFreq::BytecodeFreq(const char *const fname)
    : bytefile_(read_file(const_cast<char *>(fname)), BytefileDeleter()),
      reachable_(get_code_size(bytefile_.get())),
      jump_targets_(get_code_size(bytefile_.get())),
      OnesIdioms_(get_code_size(bytefile_.get())),
      TwosIdioms_(get_code_size(bytefile_.get())) {}

bool BytecodeFreq::is_jump(bytecode op) noexcept {
  return op == JMP || op == CJMPZ || op == CJMPNZ;
}

bool BytecodeFreq::is_call(bytecode op) noexcept {
  return op == CALL || op == CALLC;
}

bool BytecodeFreq::is_terminal(bytecode op) noexcept {
  return op == END || op == RET || op == FAIL || op == STOP;
}

void BytecodeFreq::find_reachable_instructions() {
  const size_t code_size = get_code_size(bytefile_.get());

  std::vector<size_t> workset;
  // Fill workset with publics
  for (uint32_t i = 0; i < get_public_count(bytefile_.get()); ++i) {
    uint32_t sym_offset =
        static_cast<uint32_t>(get_public_offset(bytefile_.get(), i));
    validate(sym_offset < code_size, "Invalid symbol offset", sym_offset);
    // Check duplications in public symbols
    if (!reachable_.at(sym_offset)) {
      jump_targets_[sym_offset] = 1;
      reachable_[sym_offset] = 1;
      workset.push_back(sym_offset);
    }
  }
  while (!workset.empty()) {
    uint32_t offset = workset.back();
    workset.pop_back();

    bytecode op;
    int len_ret = disassemble_instruction(stdin, bytefile_.get(),
                                          static_cast<int>(offset), &op);
    if (len_ret <= 0)
      continue; /* end of code */
    uint32_t length = static_cast<uint32_t>(len_ret);
    validate(offset + length < code_size, "Unexpected end of code",
             offset + length);
    if (is_jump(op) || is_call(op)) {
      int32_t target = get_arg(bytefile_.get(), offset);
      validate(target < code_size, "Invalid jump/call destination", offset);
      jump_targets_[target] = 1;
      if (!reachable_.at(target)) {
        reachable_[target] = 1;
        workset.push_back(target);
      }
    }
    uint32_t next_offset = offset + length;
    if (!is_terminal(op) && !is_jump(op) /* Will return to next cmd */) {
      reachable_[next_offset] = 1;
      workset.push_back(next_offset);
    }
  }
}

void BytecodeFreq::find_idioms() {
  uint32_t offset = 0;
  const size_t code_size = get_code_size(bytefile_.get());
  while (offset < code_size) {
    while (!reachable_.at(offset))
      if (++offset >= code_size)
        return; // Stop condition

    bytecode op;
    int len_ret = disassemble_instruction(stdin, bytefile_.get(),
                                          static_cast<int>(offset), &op);
    uint32_t length = static_cast<uint32_t>(len_ret);
    validate(offset + length < code_size, "Unexpected end of code",
             offset + length);

    OnesIdioms_[offset] = 1;
    uint32_t next_offset = offset + length;
    // Twos sequence (We can go to next and its not separate dot)
    if (!is_call(op) && !is_terminal(op) && reachable_.at(next_offset) &&
        !jump_targets_.at(next_offset)) {
      int next_len = disassemble_instruction(
          stdin, bytefile_.get(), static_cast<int>(next_offset), NULL);
      uint32_t next_length = static_cast<uint32_t>(next_len);
      validate(next_offset + next_length < code_size, "Unexpected end of code",
               next_offset + next_length);
      TwosIdioms_[offset] = 1;
    }
    offset += length;
  }
}

void BytecodeFreq::fill_idioms() {
  for (uint32_t offset = 0; offset < OnesIdioms_.size(); offset++) {
    // Marked
    if (OnesIdioms_[offset]) {
      bytecode op;
      int len_ret = disassemble_instruction(stdin, bytefile_.get(),
                                            static_cast<int>(offset), &op);
      OnesIdiomsFreq_[op]++;
    }
  }
  for (uint32_t offset = 0; offset < TwosIdioms_.size(); offset++) {
    // Marked
    if (TwosIdioms_[offset]) {
      bytecode op;
      bytecode next_op;
      int len_ret = disassemble_instruction(stdin, bytefile_.get(),
                                            static_cast<int>(offset), &op);
      int next_len_ret = disassemble_instruction(
          stdin, bytefile_.get(),
          static_cast<int>(offset + static_cast<uint32_t>(len_ret)), &next_op);
      TwoIdiomsFreq_[std::make_pair(op, next_op)]++;
    }
  }
}

template <typename A, typename B>
static std::pair<B, A> flip_pair(const std::pair<A, B> &p) {
  return std::pair<B, A>(p.second, p.first);
}

template <typename A, typename B>
static std::multimap<B, A> flip_map(const std::map<A, B> &src) {
  std::multimap<B, A> dst;
  std::transform(src.begin(), src.end(), std::inserter(dst, dst.begin()),
                 flip_pair<A, B>);
  return dst;
}

void BytecodeFreq::analyse() {
  find_reachable_instructions();
  find_idioms();
  fill_idioms();
  auto OnesIdiomsFreqSorted = flip_map(OnesIdiomsFreq_);
  for (auto it = OnesIdiomsFreqSorted.rbegin();
       it != OnesIdiomsFreqSorted.rend(); ++it) {
    std::cout << it->first << " - " << bytecode_string_view(it->second)
              << std::endl;
  }
  auto TwosIdiomsFreqSorted = flip_map(TwoIdiomsFreq_);
  for (auto it = TwosIdiomsFreqSorted.rbegin();
       it != TwosIdiomsFreqSorted.rend(); ++it) {
    std::cout << it->first << " - (" << bytecode_string_view(it->second.first)
              << ", " << bytecode_string_view(it->second.second) << ")"
              << std::endl;
  }
}
