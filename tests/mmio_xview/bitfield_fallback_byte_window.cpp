#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

// tests/mmio_xview/bitfield_fallback_byte_window.cpp
//
// Contract (§12):
//   For bitfield integer fields in xview:
//     If the bitfield is NOT contained within a single bus word, the implementation
//     falls back to a byte-window algorithm:
//       - volatile byte reads into a temporary window
//       - update bitfield bits using the library's LE byte-stream bit numbering model
//       - volatile byte writes back
//     It preserves bits outside the field within the touched bytes.
//
// This test validates the fallback path with bitfields that cross a bus-word boundary.
// We check:
//
//   - The exact nibble/byte updates for a classic "cross word at bit 28" field.
//   - Preservation of unrelated bits within the touched bytes.
//   - No modification outside the minimal touched window.
//   - Readback returns the expected masked value.
//   - A stress loop with many patterns catches off-by-one in window selection.
//
// Note: we validate observable memory effects; we do not attempt to prove transaction widths.
// The point is semantic correctness for the fallback window algorithm.

#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline void set_u8(std::byte& b, unsigned v) {
  b = std::byte{static_cast<unsigned char>(v & 0xFFu)};
}

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::bits == 32);
  static_assert(Bus::align == 4);

  // Field spans bits 28..36 => last 4 bits of byte3, first 4 bits of byte4.
  // This crosses bus32 word boundary between bytes [0..3] and [4..7].
  //
  // Layout total 8 bytes so we have a full second word as guard.
  using P = mad::packet<
    mad::pad_bits<28>,
    mad::ubits<8, "cross8">,
    mad::pad_bits< (64 - 36) > // pad to 64 bits total (8 bytes)
  >;
  static_assert(P::total_bytes == 8);

  using Cfg = mad::reg::cfg_enforce_bus<Bus, Bus::align>;

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  // Seed with a pattern that makes nibble preservation visible:
  // word0 bytes: 0x10 0x32 0x54 0x76
  // word1 bytes: 0x98 0xBA 0xDC 0xFE
  set_u8(mem[0], 0x10); set_u8(mem[1], 0x32); set_u8(mem[2], 0x54); set_u8(mem[3], 0x76);
  set_u8(mem[4], 0x98); set_u8(mem[5], 0xBA); set_u8(mem[6], 0xDC); set_u8(mem[7], 0xFE);

  auto vx = mad::reg::make_xview<P, Cfg>(reinterpret_cast<volatile void*>(mem.data()));

  // Baseline: cross8 value should match bits 28..36 of the LE byte-stream integer.
  // Concretely, it's: high nibble of mem[3] (0x7) as low 4 bits,
  // then low nibble of mem[4] (0x8) as high 4 bits => 0x87.
  const std::uint32_t baseline = static_cast<std::uint32_t>(vx.get<"cross8">());
  assert(baseline == 0x87u);

  // Write 0xA5 => low nibble goes into mem[3] high nibble; high nibble into mem[4] low nibble.
  vx.set<"cross8">(0xA5u);
  // mem[3] was 0x76 => low nibble 0x6 preserved, high nibble becomes 0x5 => 0x56
  // mem[4] was 0x98 => high nibble 0x9 preserved, low nibble becomes 0xA => 0x9A
  assert(u8(mem[3]) == 0x56u);
  assert(u8(mem[4]) == 0x9Au);

  // Other bytes must be unchanged.
  assert(u8(mem[0]) == 0x10u);
  assert(u8(mem[1]) == 0x32u);
  assert(u8(mem[2]) == 0x54u);
  assert(u8(mem[5]) == 0xBAu);
  assert(u8(mem[6]) == 0xDCu);
  assert(u8(mem[7]) == 0xFEu);

  // Readback should be 0xA5 (masked to 8 bits, exact here).
  assert(static_cast<std::uint32_t>(vx.get<"cross8">()) == 0xA5u);

  // Truncation: setting larger value stores low 8 bits.
  vx.set<"cross8">(0x1A5u);
  assert(static_cast<std::uint32_t>(vx.get<"cross8">()) == 0xA5u);

  // Stress loop: for each pattern, compute expected nibble effects and validate.
  for (std::uint32_t i = 0; i < 512; ++i) {
    // Re-seed with changing patterns.
    const unsigned b0 = static_cast<unsigned>((0x10u + i) & 0xFFu);
    const unsigned b1 = static_cast<unsigned>((0x21u ^ (i * 3u)) & 0xFFu);
    const unsigned b2 = static_cast<unsigned>((0x32u + (i >> 1)) & 0xFFu);
    const unsigned b3 = static_cast<unsigned>((0x43u ^ (i * 5u)) & 0xFFu);
    const unsigned b4 = static_cast<unsigned>((0x54u ^ (i * 7u)) & 0xFFu);
    const unsigned b5 = static_cast<unsigned>((0x65u + (i >> 2)) & 0xFFu);
    const unsigned b6 = static_cast<unsigned>((0x76u ^ (i * 11u)) & 0xFFu);
    const unsigned b7 = static_cast<unsigned>((0x87u + (i * 13u)) & 0xFFu);

    set_u8(mem[0], b0); set_u8(mem[1], b1); set_u8(mem[2], b2); set_u8(mem[3], b3);
    set_u8(mem[4], b4); set_u8(mem[5], b5); set_u8(mem[6], b6); set_u8(mem[7], b7);

    const std::uint32_t v = (i * 97u) ^ 0xA5u; // many values, will be truncated to 8 bits by set
    const unsigned want = static_cast<unsigned>(v & 0xFFu);

    vx.set<"cross8">(v);

    // Expected nibble update:
    // mem[3] high nibble = low nibble of want
    // mem[4] low nibble  = high nibble of want
    const unsigned want_lo = (want & 0x0Fu);
    const unsigned want_hi = ((want >> 4) & 0x0Fu);

    const unsigned exp3 = (b3 & 0x0Fu) | (want_lo << 4);
    const unsigned exp4 = (b4 & 0xF0u) | (want_hi << 0);

    assert(u8(mem[3]) == exp3);
    assert(u8(mem[4]) == exp4);

    // No other bytes touched.
    assert(u8(mem[0]) == b0);
    assert(u8(mem[1]) == b1);
    assert(u8(mem[2]) == b2);
    assert(u8(mem[5]) == b5);
    assert(u8(mem[6]) == b6);
    assert(u8(mem[7]) == b7);

    // Readback matches masked value.
    assert(static_cast<unsigned>(vx.get<"cross8">()) == want);
  }

  return 0;
}
