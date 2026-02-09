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

static uint32_t idiom1_len_bytes(const bytefile *bf, uint32_t pos) {
  int l = disassemble_instruction(stdin, bf, (int)pos, nullptr);
  return (l > 0) ? (uint32_t)l : 0;
}

static uint32_t idiom2_len_bytes(const bytefile *bf, uint32_t pos) {
  uint32_t l1 = idiom1_len_bytes(bf, pos);
  if (!l1)
    return 0;
  uint32_t l2 = idiom1_len_bytes(bf, pos + l1);
  if (!l2)
    return 0;
  return l1 + l2;
}

struct SingleBytesLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    uint32_t la = idiom1_len_bytes(bf, a.first);
    uint32_t lb = idiom1_len_bytes(bf, b.first);

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

static bool single_bytes_equal(const bytefile *bf, uint32_t pa, uint32_t pb) {
  uint32_t la = idiom1_len_bytes(bf, pa);
  uint32_t lb = idiom1_len_bytes(bf, pb);
  if (la != lb)
    return false;

  const uint8_t *ba = nullptr, *bb = nullptr;
  if (get_bytes(bf, pa, la, &ba) != 0)
    throw std::runtime_error("get_bytes");
  if (get_bytes(bf, pb, lb, &bb) != 0)
    throw std::runtime_error("get_bytes");
  return std::memcmp(ba, bb, la) == 0;
}

struct FreqThenBytesLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    if (a.second != b.second)
      return a.second > b.second;     // freq
    return SingleBytesLess{bf}(a, b); // tie-break
  }
};

struct DoubleBytesLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    uint32_t la = idiom2_len_bytes(bf, a.first);
    uint32_t lb = idiom2_len_bytes(bf, b.first);

    const uint8_t *ba = nullptr, *bb = nullptr;
    if (get_bytes(bf, a.first, la, &ba) != 0)
      throw std::runtime_error("get_bytes");
    if (get_bytes(bf, b.first, lb, &bb) != 0)
      throw std::runtime_error("get_bytes");

    size_t m = std::min<size_t>(la, lb);
    int c = std::memcmp(ba, bb, m);
    if (c != 0)
      return c < 0;
    if (la != lb)
      return la < lb;
    return a.first < b.first; // детерминизм
  }
};

static bool double_bytes_equal(const bytefile *bf, uint32_t pa, uint32_t pb) {
  uint32_t la = idiom2_len_bytes(bf, pa);
  uint32_t lb = idiom2_len_bytes(bf, pb);
  if (la != lb)
    return false;

  const uint8_t *ba = nullptr, *bb = nullptr;
  if (get_bytes(bf, pa, la, &ba) != 0)
    throw std::runtime_error("get_bytes");
  if (get_bytes(bf, pb, lb, &bb) != 0)
    throw std::runtime_error("get_bytes");
  return std::memcmp(ba, bb, la) == 0;
}

struct FreqThenDoubleBytesLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    if (a.second != b.second)
      return a.second > b.second;
    return DoubleBytesLess{bf}(a, b);
  }
};

template <class BytesLess, class BytesEq>
static void
process_idioms_inplace(std::vector<std::pair<uint32_t, uint32_t>> &v,
                       const bytefile *bf, BytesLess less, BytesEq eq) {
  size_t w = 0;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i].second != 0)
      v[w++] = v[i];
  v.resize(w);

  std::sort(v.begin(), v.end(), less);

  size_t out = 0;
  for (size_t i = 0; i < v.size();) {
    size_t j = i + 1;
    while (j < v.size() && eq(bf, v[i].first, v[j].first))
      ++j;
    v[out++] = {v[i].first, (uint32_t)(j - i)};
    i = j;
  }
  v.resize(out);
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

void BytecodeFreq::find_idioms_single() {
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
    offset += length;
  }
}

void BytecodeFreq::find_idioms_double() {
  uint32_t offset = 0;
  const size_t code_size = get_code_size(bytefile_.get());

  while (offset < code_size) {
    while (!reachable_.at(offset))
      if (++offset >= code_size)
        return;

    bytecode op;
    int len_ret =
        disassemble_instruction(stdin, bytefile_.get(), (int)offset, &op);
    if (len_ret <= 0) {
      ++offset;
      continue;
    }

    uint32_t length = (uint32_t)len_ret;
    validate(offset + length <= code_size, "Unexpected end of code",
             offset + length);

    uint32_t next_offset = offset + length;

    if (!is_call(op) && !is_terminal(op) && next_offset < code_size &&
        reachable_.at(next_offset) && !jump_targets_.at(next_offset)) {

      uint32_t l2 = idiom1_len_bytes(bytefile_.get(), next_offset);
      validate(l2 > 0 && next_offset + l2 <= code_size,
               "Unexpected end of code", next_offset);

      Idioms_[offset] = {offset, 1};
    }

    offset += length;
  }
}

void BytecodeFreq::analyse() {
  find_reachable_instructions();

  const size_t code_size = get_code_size(bytefile_.get());

  // ---------- SINGLE ----------
  std::fill(Idioms_.begin(), Idioms_.end(),
            std::pair<uint32_t, uint32_t>{0u, 0u});
  find_idioms_single();

  process_idioms_inplace(Idioms_, bytefile_.get(),
                         SingleBytesLess{bytefile_.get()}, single_bytes_equal);

  std::sort(Idioms_.begin(), Idioms_.end(), FreqThenBytesLess{bytefile_.get()});
  for (auto &[pos, cnt] : Idioms_) {
    std::cout << cnt << " ";
    disassemble_instruction(stdout, bytefile_.get(), (int)pos, nullptr);
  }

  // ---------- DOUBLE ----------
  Idioms_.assign(code_size, {0u, 0u});
  find_idioms_double();

  // compact/sort/squash
  // (сначала сделаем без финального сортировки, потом отсортируем как надо)
  // Можно просто повторить process_idioms_inplace, но печать для double другая:
  {
    // compact
    size_t w = 0;
    for (size_t i = 0; i < Idioms_.size(); ++i)
      if (Idioms_[i].second != 0)
        Idioms_[w++] = Idioms_[i];
    Idioms_.resize(w);

    std::sort(Idioms_.begin(), Idioms_.end(), DoubleBytesLess{bytefile_.get()});

    // squash
    size_t out = 0;
    for (size_t i = 0; i < Idioms_.size();) {
      size_t j = i + 1;
      while (j < Idioms_.size() &&
             double_bytes_equal(bytefile_.get(), Idioms_[i].first,
                                Idioms_[j].first)) {
        ++j;
      }
      Idioms_[out++] = {Idioms_[i].first, (uint32_t)(j - i)};
      i = j;
    }
    Idioms_.resize(out);

    // sort by freq desc (+ tie by bytes)
    std::sort(Idioms_.begin(), Idioms_.end(),
              FreqThenDoubleBytesLess{bytefile_.get()});

    // print 2 instructions
    for (auto &[pos, cnt] : Idioms_) {
      std::cout << cnt << " ";
      uint32_t l1 =
          disassemble_instruction(stdout, bytefile_.get(), (int)pos, nullptr);
      std::cout << "; ";
      disassemble_instruction(stdout, bytefile_.get(), (int)(pos + l1),
                              nullptr);
    }
  }
}
