#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "madpacket.hpp"

// tests/mmio/bitfield_fallback_byte_window.cpp
//
// Contract (§10):
//   For bitfield integer fields in reg::view:
//     - If the bitfield is NOT fully contained within one bus word, the implementation
//       falls back to a byte-window read-modify-write:
//         read each touched byte (volatile), update the bit window, then write each byte back (volatile)
//     - Bits outside the field within the touched bytes are preserved.
//     - The touched window is the minimal byte window that contains the field's bit range.
//
// This test constructs a bitfield that crosses a 32-bit bus word boundary and validates
// the observable byte-level preservation properties.
//
// Specifically, we place an 8-bit bitfield starting at bit offset 28, which spans:
//   - byte 3 (bits 28..31 => high nibble of byte3)
//   - byte 4 (bits 32..35 => low nibble of byte4)
// That crosses the boundary between word0 (bytes 0..3) and word1 (bytes 4..7), forcing fallback.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline void store_bytes(std::byte* p, std::initializer_list<unsigned> bytes) {
  std::size_t i = 0;
  for (unsigned b : bytes) p[i++] = std::byte{static_cast<unsigned char>(b)};
}

static inline std::byte b(unsigned x) { return std::byte{static_cast<unsigned char>(x)}; }

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::bits == 32);

  using P = mad::packet<
    mad::pad_bits<28>,
    mad::ubits<8, "cross">,
    mad::pad_bits<28>, // pad to reach 64 bits total (optional, ensures we have at least 8 bytes)
    mad::u8<"guard">
  >;

  // total bits = 28 + 8 + 28 + 8 = 72 bits => 9 bytes
  static_assert(P::total_bytes == 9);

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  // Initialize with a pattern so we can see what changes.
  store_bytes(mem.data(), {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x55});

  volatile std::byte* vbase = reinterpret_cast<volatile std::byte*>(mem.data());
  auto v = mad::reg::make_view<P, Bus>(reinterpret_cast<volatile void*>(vbase));

  // The field "cross" spans byte3 high nibble and byte4 low nibble.
  // We'll write value 0xA5 (1010 0101).
  //   low 4 bits  (0x5) go into byte3 bits 4..7 (high nibble)
  //   high 4 bits (0xA) go into byte4 bits 0..3 (low nibble)
  const unsigned newv = 0xA5u;
  v.set<"cross">(newv);

  // Validate get returns the stored value.
  assert(v.get<"cross">() == newv);

  // Expected byte preservation:
  // byte3 original = 0x76 => low nibble 0x6 must be preserved; high nibble becomes 0x5
  // byte4 original = 0x98 => high nibble 0x9 must be preserved; low nibble becomes 0xA
  const unsigned byte3_expected = (0x76u & 0x0Fu) | ((newv & 0x0Fu) << 4);
  const unsigned byte4_expected = (0x98u & 0xF0u) | ((newv >> 4) & 0x0Fu);

  assert(u8(mem[3]) == byte3_expected);
  assert(u8(mem[4]) == byte4_expected);

  // Bytes not in the minimal window [3..4] must be unchanged.
  assert(u8(mem[0]) == 0x10u);
  assert(u8(mem[1]) == 0x32u);
  assert(u8(mem[2]) == 0x54u);
  assert(u8(mem[5]) == 0xBAu);
  assert(u8(mem[6]) == 0xDCu);
  assert(u8(mem[7]) == 0xFEu);
  assert(u8(mem[8]) == 0x55u); // guard byte after pads

  // Additional robustness: set another value and ensure preserved nibbles update correctly.
  const unsigned newv2 = 0x0Fu; // low nibble F, high nibble 0
  v.set<"cross">(newv2);
  assert(v.get<"cross">() == newv2);

  const unsigned byte3_expected2 = (byte3_expected & 0x0Fu) | ((newv2 & 0x0Fu) << 4);
  const unsigned byte4_expected2 = (byte4_expected & 0xF0u) | ((newv2 >> 4) & 0x0Fu);

  assert(u8(mem[3]) == byte3_expected2);
  assert(u8(mem[4]) == byte4_expected2);

  // Still no change outside [3..4].
  assert(u8(mem[0]) == 0x10u);
  assert(u8(mem[1]) == 0x32u);
  assert(u8(mem[2]) == 0x54u);
  assert(u8(mem[5]) == 0xBAu);
  assert(u8(mem[6]) == 0xDCu);
  assert(u8(mem[7]) == 0xFEu);
  assert(u8(mem[8]) == 0x55u);

  return 0;
}
