#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

// tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp
//
// Contract (§12):
//   For bitfield integer fields in xview:
//     If the bitfield is fully contained within a single bus word, xview performs
//     a bus-word read, modifies the bit window in a little-endian byte-stream numeric
//     representation of that bus word, and performs a bus-word write back.
//   This is a logical bus-word RMW.
//
// This test validates the *bit-exact* effects of that one-word RMW:
//   - Only the targeted bit window is modified.
//   - Bits outside the window in the same bus word are preserved.
//   - The LE byte-stream bit numbering model is obeyed.
//   - A second bus word adjacent in memory is not affected.
//   - Truncation to field width occurs (masking).

#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline std::uint32_t le32_from_bytes(const std::byte* p) {
  return static_cast<std::uint32_t>(u8(p[0]) | (u8(p[1]) << 8) | (u8(p[2]) << 16) | (u8(p[3]) << 24));
}

static inline void store_le32(std::byte* p, std::uint32_t v) {
  p[0] = std::byte{static_cast<unsigned char>((v >> 0) & 0xFFu)};
  p[1] = std::byte{static_cast<unsigned char>((v >> 8) & 0xFFu)};
  p[2] = std::byte{static_cast<unsigned char>((v >> 16) & 0xFFu)};
  p[3] = std::byte{static_cast<unsigned char>((v >> 24) & 0xFFu)};
}

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::bits == 32);
  static_assert(Bus::align == 4);

  // Bitfield begins at bit offset 5 and is 10 bits wide:
  //   bits 0..4   : outside (preserve)
  //   bits 5..14  : bf10 field
  //   bits 15..31 : outside (preserve)
  //
  // Add a second u32 word to ensure it isn't touched by the one-word RMW on word0.
  using P = mad::packet<
    mad::pad_bits<5>,
    mad::ubits<10, "bf10">,
    mad::pad_bits<17>,
    mad::u32<"word1">
  >;
  static_assert(P::total_bytes == 8);

  using Cfg = mad::reg::cfg_enforce_bus<Bus, Bus::align>;

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};

  // Seed word0 and word1 with distinct patterns.
  store_le32(mem.data() + 0, 0xDDBBCCAAu); // bytes: AA BB CC DD
  store_le32(mem.data() + 4, 0x11223344u); // bytes: 44 33 22 11

  auto vx = mad::reg::make_xview<P, Cfg>(reinterpret_cast<volatile void*>(mem.data()));

  // Baseline: bf10 should equal bits[5..15) of word0 interpreted as LE stream numeric.
  const std::uint32_t w0_before = le32_from_bytes(mem.data() + 0);
  const std::uint32_t bf_before = (w0_before >> 5) & ((1u << 10) - 1u);
  assert(static_cast<std::uint32_t>(vx.get<"bf10">()) == bf_before);
  assert(static_cast<std::uint32_t>(vx.get<"word1">()) == 0x11223344u);

  // Write a value within range.
  vx.set<"bf10">(0x155u); // 10-bit value

  const std::uint32_t mask10 = (1u << 10) - 1u;
  const std::uint32_t m = (mask10 << 5);
  const std::uint32_t expected_w0 = (w0_before & ~m) | ((0x155u & mask10) << 5);

  const std::uint32_t w0_after = le32_from_bytes(mem.data() + 0);
  assert(w0_after == expected_w0);

  // Bits outside field preserved.
  assert((w0_after & ~m) == (w0_before & ~m));

  // word1 unchanged.
  assert(le32_from_bytes(mem.data() + 4) == 0x11223344u);

  // Readback matches masked value.
  assert(static_cast<std::uint32_t>(vx.get<"bf10">()) == (0x155u & mask10));

  // Write out-of-range to ensure truncation.
  vx.set<"bf10">(0xFFFFu);
  const std::uint32_t w0_after2 = le32_from_bytes(mem.data() + 0);
  const std::uint32_t expected2 = (w0_after & ~m) | ((0xFFFFu & mask10) << 5);
  assert(w0_after2 == expected2);
  assert(static_cast<std::uint32_t>(vx.get<"bf10">()) == mask10);

  // Many patterns to catch off-by-one or wrong bit numbering.
  for (std::uint32_t seed = 0; seed < 128; ++seed) {
    const std::uint32_t base = 0xA5A50000u ^ (seed * 0x10203u);
    store_le32(mem.data() + 0, base);
    store_le32(mem.data() + 4, 0xCAFEBABEu ^ seed);

    const std::uint32_t w0 = le32_from_bytes(mem.data() + 0);
    const std::uint32_t v  = (seed * 73u) ^ 0x3FFu; // likely exceeds 10 bits
    vx.set<"bf10">(v);

    const std::uint32_t want = (w0 & ~m) | ((v & mask10) << 5);
    const std::uint32_t got  = le32_from_bytes(mem.data() + 0);

    assert(got == want);
    assert((got & ~m) == (w0 & ~m));
    assert(le32_from_bytes(mem.data() + 4) == (0xCAFEBABEu ^ seed));
    assert(static_cast<std::uint32_t>(vx.get<"bf10">()) == (v & mask10));
  }

  return 0;
}
