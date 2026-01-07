#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include "madpacket.hpp"

// Golden tests for cross-byte bitfields using the contract bit numbering model:
// bit 0 = LSB of byte 0, increasing within byte, then across bytes in increasing address order.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  // Case 1: 10-bit field starting at bit offset 3 (spans 2 bytes).
  // Layout: head[3] | x[10] | tail[3]  => total 16 bits (2 bytes)
  using P = packet<
    u3<"head">,
    u10<"x">,
    u3<"tail">
  >;

  static_assert(P::total_bits == 16);
  static_assert(P::total_bytes == 2);

  {
    std::array<std::byte, P::total_bytes> buf{std::byte{0}, std::byte{0}};
    auto v = make_view<P>(buf.data(), buf.size());

    // Chosen values with a distinctive bit pattern.
    // head = 0b111
    // x    = 0x2AA (10 bits) = 0b10'1010'1010 (LSB-first pattern 0,1,0,1,0,1,0,1,0,1)
    // tail = 0b101
    v.set<"head">(7);
    v.set<"x">(0x2AA);
    v.set<"tail">(5);

    // Expected bytes derived from the contract mapping:
    // byte0 = 0x57, byte1 = 0xB5
    assert(u8(buf[0]) == 0x57u);
    assert(u8(buf[1]) == 0xB5u);

    // Read-back must match.
    assert(v.get<"head">() == 7u);
    assert(v.get<"x">() == 0x2AAu);
    assert(v.get<"tail">() == 5u);

    // Preservation check (RMW window correctness):
    // Start from all-ones, clear x -> head and tail bits must remain ones.
    buf[0] = std::byte{0xFF};
    buf[1] = std::byte{0xFF};
    v.set<"x">(0);

    // head occupies bits 0..2 => keep 1s => byte0 low 3 bits = 0b111 => 0x07
    // x occupies bits 3..12 cleared => rest of bytes except tail
    // tail occupies bits 13..15 => keep 1s => byte1 high 3 bits set => 0xE0
    assert(u8(buf[0]) == 0x07u);
    assert(u8(buf[1]) == 0xE0u);

    // Now set x to all-ones (10 bits => 0x3FF) and ensure head/tail preserved.
    v.set<"x">(0x3FF);

    // head still 0x07 in low bits, x fills bits 3..12 with ones
    // byte0: bits0..7 => head(111) + x bits0..4 (11111) => 0b11111111 = 0xFF
    // byte1: bits8..12 are ones (x bits5..9), bits13..15 are ones (tail) => 0xFF
    assert(u8(buf[0]) == 0xFFu);
    assert(u8(buf[1]) == 0xFFu);

    // Sanity: read-back after masking rules.
    assert(v.get<"x">() == 0x3FFu);
  }

  // Case 2: 19-bit field starting at bit offset 5 (spans 3 bytes).
  // Layout: head[5] | x[19] | tail[4] => total 28 bits (4 bytes)
  using Q = packet<
    u5<"head">,
    u19<"x">,
    u4<"tail">
  >;

  static_assert(Q::total_bits == 28);
  static_assert(Q::total_bytes == 4);

  {
    std::array<std::byte, Q::total_bytes> buf{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    auto v = make_view<Q>(buf.data(), buf.size());

    // Chosen values:
    // head = 0b10101 (21)
    // x    = 0x4ABCD (fits in 19 bits)
    // tail = 0xD (0b1101)
    v.set<"head">(0b10101);
    v.set<"x">(0x4ABCDu);
    v.set<"tail">(0xDu);

    // Expected bytes under le-stream bit numbering:
    // byte0=0xB5, byte1=0x79, byte2=0x95, byte3=0x0D
    assert(u8(buf[0]) == 0xB5u);
    assert(u8(buf[1]) == 0x79u);
    assert(u8(buf[2]) == 0x95u);
    assert(u8(buf[3]) == 0x0Du);

    // Read-back must match (no truncation needed for these chosen values).
    assert(v.get<"head">() == 21u);
    assert(v.get<"x">() == 0x4ABCDu);
    assert(v.get<"tail">() == 0xDu);

    // Preservation / window minimality-ish check:
    // Clearing x should not modify head bits (0..4) nor tail bits (24..27).
    buf[0] = std::byte{0xFF};
    buf[1] = std::byte{0xFF};
    buf[2] = std::byte{0xFF};
    buf[3] = std::byte{0xFF};

    v.set<"x">(0);

    // head (bits0..4) preserved -> byte0 low 5 bits = 0x1F, x clears bits5..23
    // byte3 is outside x window (x ends at bit23) so it should remain untouched (0xFF).
    assert(u8(buf[0]) == 0x1Fu);
    assert(u8(buf[1]) == 0x00u);
    assert(u8(buf[2]) == 0x00u);
    assert(u8(buf[3]) == 0xFFu);

    // Now set tail and ensure high 4 bits in byte3 are preserved (RMW within byte3).
    v.set<"tail">(0x0u);
    assert((u8(buf[3]) & 0x0Fu) == 0x00u);
    assert((u8(buf[3]) & 0xF0u) == 0xF0u); // untouched bits remain 1

    v.set<"tail">(0xAu);
    assert((u8(buf[3]) & 0x0Fu) == 0x0Au);
    assert((u8(buf[3]) & 0xF0u) == 0xF0u);
  }

  return 0;
}
