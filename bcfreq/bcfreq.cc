#include <algorithm>
#include <iostream>
#include <sstream>

#include "bcfreq.hh"

// 31 bit pure address -> 32 bit address with 1 bit for JMP mark
#define BOX_WITH_JMP_MARK(x, MARK) ((x) | (MARK << 31))
#define UNBOX_WITH_JMP_MARK(x) ((x) & ~(1 << 31))
#define GET_JMP_MARK(x) ((x) >> 31)

static void validate(bool condition, const std::string &message,
                     uint32_t bytecode_offset) {
  if (!condition) {
    throw std::runtime_error(std::to_string(bytecode_offset) + ": " + message);
  }
}

static uint32_t idiom1_len_bytes(const bytefile *bf, uint32_t pos) {
  return disassemble_instruction(stdin, bf, (int)pos, nullptr);
}

static uint32_t idiom2_len_bytes(const bytefile *bf, uint32_t pos) {
  int l1 = idiom1_len_bytes(bf, pos);
  int l2 = idiom1_len_bytes(bf, pos + l1);

  return static_cast<uint32_t>(l1) + static_cast<uint32_t>(l2);
}

template <uint32_t (*IdiomLen)(const bytefile *, uint32_t)>
struct IdiomBytesLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    const uint32_t pa = UNBOX_WITH_JMP_MARK(a.first);
    const uint32_t pb = UNBOX_WITH_JMP_MARK(b.first);
    const uint32_t la = IdiomLen(bf, pa);
    const uint32_t lb = IdiomLen(bf, pb);

    const uint8_t *ba = nullptr, *bb = nullptr;
    validate(get_bytes(bf, pa, la, &ba) == 0, "not able to get bytes", pa);
    validate(get_bytes(bf, pb, lb, &bb) == 0, "not able to get bytes", pb);

    const size_t m = std::min<size_t>(la, lb);
    const int c = std::memcmp(ba, bb, m);
    if (c != 0)
      return c < 0;
    return la < lb;
  }
};

template <uint32_t (*IdiomLen)(const bytefile *, uint32_t)>
static bool idiom_bytes_equal(const bytefile *bf, uint32_t pa, uint32_t pb) {
  pa = UNBOX_WITH_JMP_MARK(pa);
  pb = UNBOX_WITH_JMP_MARK(pb);
  uint32_t la = IdiomLen(bf, pa);
  uint32_t lb = IdiomLen(bf, pb);
  if (la != lb)
    return false;

  const uint8_t *ba = nullptr, *bb = nullptr;
  validate(get_bytes(bf, pa, la, &ba) == 0, "not able to get bytes", pa);
  validate(get_bytes(bf, pb, lb, &bb) == 0, "not able to get bytes", pb);

  return std::memcmp(ba, bb, la) == 0;
}

template <uint32_t (*IdiomLen)(const bytefile *, uint32_t)>
struct IdiomFreqLess {
  const bytefile *bf;

  bool operator()(const std::pair<uint32_t, uint32_t> &a,
                  const std::pair<uint32_t, uint32_t> &b) const {
    if (a.second != b.second)
      return a.second > b.second;
    return IdiomBytesLess<IdiomLen>{bf}(a, b);
  }
};

template <class BytesLess, class BytesEq, class FreqLess>
static void
process_idioms_inplace(std::vector<std::pair<uint32_t, uint32_t>> &v,
                       const bytefile *bf, BytesLess less, BytesEq eq,
                       FreqLess frless) {

  // 1. Sort (N * logN), N - reached bytecodes
  std::sort(v.begin(), v.end(), less);

  // 2. Squash (N), N - reached bytecodes
  size_t out = 0;
  for (size_t i = 0; i < v.size();) {
    size_t j = i + 1;
    while (j < v.size() && eq(bf, v[i].first, v[j].first))
      ++j;
    v[out++] = {v[i].first, (uint32_t)(j - i)};
    i = j;
  }

  // 3. Dedup (U), U - duplications count
  v.resize(out);

  // 4. Sort (D * logD), D - deduplicated reached idioms
  std::sort(v.begin(), v.end(), frless);
}

BytecodeFreq::BytecodeFreq(const char *const fname)
    : bytefile_(read_file(const_cast<char *>(fname)), BytefileDeleter()) {}
BytecodeFreq::~BytecodeFreq() {}

bool BytecodeFreq::is_jump(bytecode op) noexcept {
  return op == JMP || op == CJMPZ || op == CJMPNZ;
}

bool BytecodeFreq::is_call(bytecode op) noexcept {
  return op == CALL || op == CALLC;
}

bool BytecodeFreq::is_terminal(bytecode op) noexcept {
  return op == JMP || op == END || op == RET || op == FAIL || op == STOP;
}

void BytecodeFreq::find_reachable_instructions(
    std::vector<bool> &reachable, std::vector<bool> &jump_targets) {
  const size_t code_size = get_code_size(bytefile_.get());

  std::vector<size_t> workset;
  // Fill workset with publics
  for (uint32_t i = 0; i < get_public_count(bytefile_.get()); ++i) {
    uint32_t sym_offset =
        static_cast<uint32_t>(get_public_offset(bytefile_.get(), i));
    validate(sym_offset < code_size, "Invalid symbol offset", sym_offset);
    // Check duplications in public symbols
    if (!reachable.at(sym_offset)) {
      jump_targets[sym_offset] = true;
      reachable[sym_offset] = true;
      workset.push_back(sym_offset);
    }
  }
  while (!workset.empty()) {
    uint32_t offset = workset.back();
    workset.pop_back();

    bytecode op;
    int len_ret = disassemble_instruction(stdin, bytefile_.get(),
                                          static_cast<int>(offset), &op);
    uint32_t length = static_cast<uint32_t>(len_ret);
    validate(offset + length < code_size, "Unexpected end of code",
             offset + length);
    if (is_jump(op) || is_call(op)) {
      int32_t target_i = get_arg(bytefile_.get(), offset);
      validate(target_i >= 0 && static_cast<uint32_t>(target_i) < code_size,
               "Invalid jump/call destination",
               static_cast<uint32_t>(target_i));
      uint32_t target = static_cast<uint32_t>(target_i);
      jump_targets[target] = true;
      if (!reachable.at(target)) {
        reachable[target] = true;
        workset.push_back(target);
      }
    }
    if (!is_terminal(op)) {
      uint32_t next_offset = offset + length;
      if (is_call(op)) {
        jump_targets[next_offset] = true;
      }
      if (!reachable.at(next_offset)) {
        reachable[next_offset] = true;
        workset.push_back(next_offset);
      }
    }
  }
}

void BytecodeFreq::find_idioms_single(
    std::vector<std::pair<uint32_t, uint32_t>> &v,
    const std::vector<bool> &reachable, const std::vector<bool> &jump_targets) {
  uint32_t offset = 0;
  const size_t code_size = get_code_size(bytefile_.get());
  while (offset < code_size) {
    while (!reachable.at(offset))
      if (++offset >= code_size)
        return; // Stop condition

    bytecode op;
    int len_ret = disassemble_instruction(stdin, bytefile_.get(),
                                          static_cast<int>(offset), &op);
    uint32_t length = static_cast<uint32_t>(len_ret);
    validate(offset + length < code_size, "Unexpected end of code",
             offset + length);

    if (jump_targets.at(offset)) {
      v[offset] = {BOX_WITH_JMP_MARK(offset, 1), 1};
    } else {
      v[offset] = {BOX_WITH_JMP_MARK(offset, 0), 1};
    }

    offset += length;
  }
}

void BytecodeFreq::find_idioms_double(
    std::vector<std::pair<uint32_t, uint32_t>> &v,
    const std::vector<std::pair<uint32_t, uint32_t>> &single_idioms) {
  for (size_t i = 0; i < single_idioms.size() - 1; i++) {
    int len_ret = disassemble_instruction(
        stdin, bytefile_.get(), UNBOX_WITH_JMP_MARK(single_idioms[i].first),
        NULL);

    // NOT basic block border
    if (GET_JMP_MARK(single_idioms[i + 1].first) == 0) {
      uint32_t pos = UNBOX_WITH_JMP_MARK(single_idioms[i].first);
      v[pos] = {pos, 1};
    }
  }
}

void BytecodeFreq::analyse() {
  const size_t code_size = get_code_size(bytefile_.get());
  std::vector<std::pair<uint32_t, uint32_t>> single_idioms(
      code_size, std::pair<uint32_t, uint32_t>{0u, 0u}); // 8X file size
  do {
    std::vector<bool> reachable(code_size);    // 1/8X file size
    std::vector<bool> jump_targets(code_size); // 1/8X file size
    find_reachable_instructions(reachable, jump_targets);
    find_idioms_single(single_idioms, reachable, jump_targets);
  } while (0); // Summary 8.25X file size in inner scope/ 8X in outer scope

  // Alloc 8X -> Summary 16X in scope
  std::vector<std::pair<uint32_t, uint32_t>> double_idioms(code_size, {0u, 0u});

  // Trim single
  size_t w = 0;
  for (size_t i = 0; i < single_idioms.size(); ++i)
    if (single_idioms[i].second != 0)
      single_idioms[w++] = single_idioms[i];
  single_idioms.resize(w);

  // Place for extend arity of idioms (3, 4 ... /*Same logic*/)
  find_idioms_double(double_idioms, single_idioms);

  // Trim double
  w = 0;
  for (size_t i = 0; i < double_idioms.size(); ++i)
    if (double_idioms[i].second != 0)
      double_idioms[w++] = double_idioms[i];
  double_idioms.resize(w);

  // Sort freq (N * logN), N - reached bytecodes
  process_idioms_inplace(single_idioms, bytefile_.get(),
                         IdiomBytesLess<&idiom1_len_bytes>{bytefile_.get()},
                         idiom_bytes_equal<&idiom1_len_bytes>,
                         IdiomFreqLess<&idiom1_len_bytes>{bytefile_.get()});

  // Sort freq (N * logN), N - reached bytecodes
  process_idioms_inplace(double_idioms, bytefile_.get(),
                         IdiomBytesLess<&idiom2_len_bytes>{bytefile_.get()},
                         idiom_bytes_equal<&idiom2_len_bytes>,
                         IdiomFreqLess<&idiom2_len_bytes>{bytefile_.get()});

  auto it1 = single_idioms.cbegin();
  auto it2 = double_idioms.cbegin();

  while (it1 != single_idioms.cend() && it2 != double_idioms.cend()) {
    const uint32_t f1 = it1->second;
    const uint32_t f2 = it2->second;

    if (f1 >= f2) {
      const uint32_t off1 = UNBOX_WITH_JMP_MARK(it1->first);
      const uint32_t mark1 = GET_JMP_MARK(it1->first);

      fprintf(stdout, "%u ", f1);
      disassemble_instruction(stdout, bytefile_.get(), (int)off1, nullptr);
      fprintf(stdout, "\n");

      ++it1;
    } else {
      const uint32_t off2 = it2->first;

      fprintf(stdout, "%u ", f2);
      int l1 =
          disassemble_instruction(stdout, bytefile_.get(), (int)off2, nullptr);
      fprintf(stdout, " -> ");
      int l2 = disassemble_instruction(stdout, bytefile_.get(),
                                       (int)(off2 + l1), nullptr);
      fprintf(stdout, "\n");

      ++it2;
    }
  }

  // tails
  while (it1 != single_idioms.cend()) {
    const uint32_t off1 = UNBOX_WITH_JMP_MARK(it1->first);
    const uint32_t mark1 = GET_JMP_MARK(it1->first);
    const uint32_t f1 = it1->second;

    fprintf(stdout, "%u ", f1);
    disassemble_instruction(stdout, bytefile_.get(), (int)off1, nullptr);
    fprintf(stdout, "\n");
    ++it1;
  }

  while (it2 != double_idioms.cend()) {
    const uint32_t off2 = it2->first;
    const uint32_t f2 = it2->second;

    fprintf(stdout, "%u ", f2);
    int l1 =
        disassemble_instruction(stdout, bytefile_.get(), (int)off2, nullptr);
    fprintf(stdout, " -> ");
    int l2 = disassemble_instruction(stdout, bytefile_.get(), (int)(off2 + l1),
                                     nullptr);
    fprintf(stdout, "\n");

    ++it2;
  }
}
