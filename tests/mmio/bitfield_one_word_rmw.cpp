#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "madpacket.hpp"

// tests/mmio/bitfield_one_word_rmw.cpp
//
// Contract (§10):
//   For bitfield integer fields in reg::view:
//     - If the bitfield is fully contained within a single bus word, the implementation
//       performs a logical bus-word RMW:
//         read bus word (LE byte-stream numeric), update bit window, write bus word back
//     - Bits outside the field within that bus word are preserved.
//
// This test validates the *observable* behavior:
//   - get extracts the correct bits from a known bus word byte pattern
//   - set updates only the selected bit window and preserves all other bits
//   - bytes outside the bus word are untouched when the field is contained within the first word

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline std::uint64_t load_le(std::byte const* p, std::size_t n) {
  std::uint64_t x = 0;
  for (std::size_t i = 0; i < n; ++i) x |= (std::uint64_t(u8(p[i])) << (8u * i));
  return x;
}

static inline void store_bytes(std::byte* p, std::initializer_list<unsigned> bytes) {
  std::size_t i = 0;
  for (unsigned b : bytes) p[i++] = std::byte{static_cast<unsigned char>(b)};
}

static inline std::uint64_t mask_bits(unsigned bits) {
  return bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
}

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::bits == 32);

  // Choose a bitfield that fits entirely within the first bus word.
  // We'll place it at bit offset 7 with width 10 => bits [7..17) within word0.
  using P = mad::packet<
    mad::pad_bits<7>,
    mad::ubits<10, "bf">,
    mad::pad_bits<15>,  // total 32 bits so far
    mad::u32<"w1">       // second word (for guard/untouched validation)
  >;

  static_assert(P::total_bytes == 4 + 4);

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};

  // Initialize word0 bytes with a known pattern and word1 with sentinels.
  store_bytes(mem.data(), {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44});

  volatile std::byte* vbase = reinterpret_cast<volatile std::byte*>(mem.data());
  auto v = mad::reg::make_view<P, Bus>(reinterpret_cast<volatile void*>(vbase));

  const std::uint64_t w0 = load_le(mem.data(), 4);
  assert(w0 == 0xDDCCBBAAull);

  const std::uint64_t expected_get = (w0 >> 7) & mask_bits(10);
  assert(v.get<"bf">() == expected_get);

  // Now set bf to a new value and check only that bit window changes.
  const std::uint64_t new_val = 0x155u; // 10-bit value (0b01_0101_0101)
  v.set<"bf">(new_val);

  const std::uint64_t m = mask_bits(10) << 7;
  const std::uint64_t expected_w1 = (w0 & ~m) | ((new_val & mask_bits(10)) << 7);

  const std::uint64_t w0_after = load_le(mem.data(), 4);
  assert(w0_after == (expected_w1 & 0xFFFFFFFFull));

  // Other bits within word0 are preserved; so recompute get from expected_w1.
  assert(v.get<"bf">() == ((expected_w1 >> 7) & mask_bits(10)));

  // Bytes outside the first bus word should be untouched by this set.
  assert(u8(mem[4]) == 0x11u);
  assert(u8(mem[5]) == 0x22u);
  assert(u8(mem[6]) == 0x33u);
  assert(u8(mem[7]) == 0x44u);

  // Additional robustness: perform another set that flips bits and verify preservation again.
  const std::uint64_t new_val2 = 0x3u;
  const std::uint64_t w_before2 = load_le(mem.data(), 4);
  v.set<"bf">(new_val2);

  const std::uint64_t expected_w2 = (w_before2 & ~m) | ((new_val2 & mask_bits(10)) << 7);
  const std::uint64_t w_after2 = load_le(mem.data(), 4);
  assert(w_after2 == (expected_w2 & 0xFFFFFFFFull));

  // Word1 still untouched.
  assert(u8(mem[4]) == 0x11u);
  assert(u8(mem[5]) == 0x22u);
  assert(u8(mem[6]) == 0x33u);
  assert(u8(mem[7]) == 0x44u);

  return 0;
}
