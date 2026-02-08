#include <algorithm>
#include <iostream>
#include <sstream>

#include "bcfreq.hh"
#include <span>

static void validate(bool condition, const std::string &message,
                     uint32_t bytecode_offset) {
  if (!condition) {
    throw new std::runtime_error(std::to_string(bytecode_offset) + ": " +
                                 message);
  }
}

template <class GetLen, class Print>
static void
count_and_print(const bytefile *bf,
                std::vector<char> &marked,        // OnesIdioms_ или TwosIdioms_
                std::vector<size_t> &freq_by_off, // IdiomsFreq_
                size_t code_size,
                GetLen get_len, // int(uint32_t off)
                Print print_off // void(uint32_t off)
) {
  size_t max_freq = 0;

  for (uint32_t off = 0; off < marked.size(); ++off) {
    if (!marked[off])
      continue;

    int len = get_len(off);
    if (len <= 0)
      continue;

    freq_by_off[off] = 1;

    const uint8_t *golden = nullptr;
    if (get_bytes(bf, off, (uint32_t)len, &golden) != 0) {
      throw std::runtime_error("get_bytes out of range");
    }

    for (uint32_t next = off + 1; next < marked.size(); ++next) {
      if (!marked[next])
        continue;

      int next_len = get_len(next);
      if (next_len != len)
        continue;

      const uint8_t *next_p = nullptr;
      if (get_bytes(bf, next, (uint32_t)next_len, &next_p) != 0) {
        throw std::runtime_error("get_bytes out of range");
      }

      // If found equal => incr cur freq and erase marked at next offset
      if (memcmp(golden, next_p, (size_t)len) == 0) {
        ++freq_by_off[off];
        marked[next] = 0;
      }
    }

    if (freq_by_off[off] > max_freq)
      max_freq = freq_by_off[off];
  }

  for (size_t f = max_freq; f >= 1; --f) {
    for (size_t off = 0; off < code_size && off < marked.size(); ++off) {
      if (!marked[off])
        continue;
      if (freq_by_off[off] != f)
        continue;

      std::cout << f << " ";
      print_off((uint32_t)off);
      std::cout << "\n";
    }
    if (f == 1)
      break;
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

void BytecodeFreq::analyse() {
  find_reachable_instructions();
  find_idioms();

  const size_t code_size = get_code_size(bytefile_.get());
  std::vector<size_t>
      IdiomsFreq_; // For each file byte -> 8 byte => 8X file size, 12X for now
  IdiomsFreq_.resize(code_size);

  // One
  count_and_print(
      bytefile_.get(), OnesIdioms_, IdiomsFreq_, code_size,
      [&](uint32_t off) -> int {
        return disassemble_instruction(stdin, bytefile_.get(), (int)off,
                                       nullptr);
      },
      [&](uint32_t off) {
        disassemble_instruction(stdout, bytefile_.get(), (int)off, nullptr);
      });

  // Two
  count_and_print(
      bytefile_.get(), TwosIdioms_, IdiomsFreq_, code_size,
      [&](uint32_t off) -> int {
        int l1 =
            disassemble_instruction(stdin, bytefile_.get(), (int)off, nullptr);
        if (l1 <= 0)
          return l1;
        int l2 = disassemble_instruction(stdin, bytefile_.get(),
                                         (int)(off + (uint32_t)l1), nullptr);
        if (l2 <= 0)
          return l2;
        return l1 + l2;
      },
      [&](uint32_t off) {
        int l1 =
            disassemble_instruction(stdout, bytefile_.get(), (int)off, nullptr);
        std::cout << "; ";
        disassemble_instruction(stdout, bytefile_.get(),
                                (int)(off + (uint32_t)l1), nullptr);
      });
}
