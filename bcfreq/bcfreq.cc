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

static uint32_t idiom_len_bytes(const bytefile *bf, uint32_t pos) {
  int l = disassemble_instruction(stdin, bf, (int)(pos), nullptr);
  if (l <= 0)
    return 0;
  return l;
}

struct SingleBytesLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    uint32_t la = idiom_len_bytes(bf, a.first);
    uint32_t lb = idiom_len_bytes(bf, b.first);

    const uint8_t *ba = nullptr, *bb = nullptr;
    if (get_bytes(bf, a.first, la, &ba) != 0)
      throw std::runtime_error("get_bytes");
    if (get_bytes(bf, b.first, lb, &bb) != 0)
      throw std::runtime_error("get_bytes");

    size_t m = std::min<size_t>(la, lb);
    int c = std::memcmp(ba, bb, m);
    if (c != 0)
      return c < 0;
    return la < lb;
  }
};

struct FreqThenBytesLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    if (a.second != b.second)
      return a.second > b.second;     // freq
    return SingleBytesLess{bf}(a, b); // tie-break
  }
};

static bool single_bytes_equal(const bytefile *bf, uint32_t pa, uint32_t pb) {
  uint32_t la = idiom_len_bytes(bf, pa);
  uint32_t lb = idiom_len_bytes(bf, pb);
  if (la != lb)
    return false;

  const uint8_t *ba = nullptr, *bb = nullptr;
  if (get_bytes(bf, pa, la, &ba) != 0)
    throw std::runtime_error("get_bytes");
  if (get_bytes(bf, pb, lb, &bb) != 0)
    throw std::runtime_error("get_bytes");
  return std::memcmp(ba, bb, la) == 0;
}

BytecodeFreq::BytecodeFreq(const char *const fname)
    : bytefile_(read_file(const_cast<char *>(fname)), BytefileDeleter()),
      reachable_(get_code_size(bytefile_.get())),
      jump_targets_(get_code_size(bytefile_.get())),
      Idioms_(get_code_size(bytefile_.get())) {}

bool BytecodeFreq::is_jump(bytecode op) noexcept {
  return op == JMP || op == CJMPZ || op == CJMPNZ;
}

bool BytecodeFreq::is_call(bytecode op) noexcept {
  return op == CALL || op == CALLC;
}

bool BytecodeFreq::is_terminal(bytecode op) noexcept {
  return op == JMP || op == END || op == RET || op == FAIL || op == STOP;
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
      int32_t target_i = get_arg(bytefile_.get(), offset);
      validate(target_i >= 0 && static_cast<uint32_t>(target_i) < code_size,
               "Invalid jump/call destination",
               static_cast<uint32_t>(target_i));
      uint32_t target = static_cast<uint32_t>(target_i);
      jump_targets_[target] = 1;
      if (!reachable_.at(target)) {
        reachable_[target] = 1;
        workset.push_back(target);
      }
    }
    if (!is_terminal(op)) {
      uint32_t next_offset = offset + length;
      if (is_call(op)) {
        jump_targets_[next_offset] = 1;
      }
      if (!reachable_.at(next_offset)) {
        reachable_[next_offset] = 1;
        workset.push_back(next_offset);
      }
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

    Idioms_[offset] = {offset, 1};
    // uint32_t next_offset = offset + length;
    // // Twos sequence (We can go to next and its not separate dot)
    // if (!is_call(op) && !is_terminal(op) && reachable_.at(next_offset) &&
    //     !jump_targets_.at(next_offset)) {
    //   int next_len = disassemble_instruction(
    //       stdin, bytefile_.get(), static_cast<int>(next_offset), NULL);
    //   uint32_t next_length = static_cast<uint32_t>(next_len);
    //   validate(next_offset + next_length < code_size, "Unexpected end of
    //   code",
    //            next_offset + next_length);
    //   Idioms_[offset].second = MARK_TWO(Idioms_[offset].second);
    // }
    offset += length;
  }
}

void BytecodeFreq::analyse() {
  find_reachable_instructions();
  find_idioms();

  // delete unused offsets
  size_t w = 0;
  for (size_t i = 0; i < Idioms_.size(); ++i) {
    if (Idioms_[i].second != 0) {
      Idioms_[w++] = Idioms_[i];
    }
  }
  Idioms_.resize(w);

  // Sort
  std::sort(Idioms_.begin(), Idioms_.end(), SingleBytesLess{bytefile_.get()});

  // Squash (vector len compares)
  size_t u = 0;
  for (size_t i = 0; i < Idioms_.size();) {
    size_t j = i + 1;
    while (j < Idioms_.size() &&
           single_bytes_equal(bytefile_.get(), Idioms_[i].first,
                              Idioms_[j].first)) {
      ++j;
    }
    Idioms_[u++] = {Idioms_[i].first, (uint32_t)(j - i)};
    i = j;
  }
  Idioms_.resize(u);

  // Output sort
  std::sort(Idioms_.begin(), Idioms_.end(), FreqThenBytesLess{bytefile_.get()});

  // Print
  for (const auto &[pos, cnt] : Idioms_) {
    disassemble_instruction(stdout, bytefile_.get(), (int)pos, nullptr);
  }
}
