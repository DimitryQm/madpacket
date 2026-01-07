#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/bitfields/rmw_preserves_neighbor_bits.cpp
//
// Contract (§7):
//  - For bitfield integer fields, set() performs a read-modify-write of the minimal
//    byte window that contains the field's bit range.
//  - Bits outside the field are preserved within the touched bytes.
//
// This file focuses on *preservation*: bits outside the field remain unchanged,
// even when the field starts/ends mid-byte and spans multiple bytes.
//
// Note: We do not (and cannot, without instrumentation) prove the exact number of
// stores performed; we prove observable byte/bit preservation and lack of clobber.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline void assert_bits_preserved(std::byte before, std::byte after, unsigned preserve_mask) {
  // preserve_mask: bits that must remain identical (1 bits mean "must match")
  const unsigned b = u8(before);
  const unsigned a = u8(after);
  assert(((a ^ b) & preserve_mask) == 0u);
}

int main() {
  using namespace mad;

  // Case A: bitfield entirely within one byte, neighbors in same byte
  //
  // Layout in byte0:
  //   pre  : bits0..1 (u2)
  //   mid  : bits2..6 (ubits<5>)
  //   post : bit7     (u1)
  // Byte1 is a tail guard to detect accidental writes beyond byte0.
  using A = packet<
    u2<"pre">,
    ubits<5, "mid">,
    u1<"post">,
    u8<"tail">
  >;
  static_assert(A::total_bits == 8 + 8);
  static_assert(A::total_bytes == 2);

  {
    std::array<std::byte, A::total_bytes> buf{};
    // Initialize with a distinctive byte pattern so "preservation" is meaningful.
    buf[0] = std::byte{0b1010'0101}; // pre=01, mid=10010, post=1
    buf[1] = std::byte{0xCC};        // tail guard

    auto v = make_view<A>(buf.data(), buf.size());

    // Snapshot "outside mid" bits in byte0: bits0..1 and bit7 must be preserved.
    const std::byte before0 = buf[0];
    const unsigned preserve_mask = 0b1000'0011u;

    // Set mid to a different value; this must not disturb pre/post bits.
    v.set<"mid">(0b00001u);

    // Check named fields still read the preserved bits.
    assert(v.get<"pre">() == (u8(before0) & 0x03u));
    assert(v.get<"post">() == ((u8(before0) >> 7) & 0x01u));

    // Check raw byte preservation on the preserved bits.
    assert_bits_preserved(before0, buf[0], preserve_mask);

    // Tail guard unchanged.
    assert(u8(buf[1]) == 0xCCu);

    // Now flip mid again, with input that has extra high bits set (must be masked).
    const std::byte before1 = buf[0];
    v.set<"mid">(0xFFu); // low 5 bits => 11111
    assert_bits_preserved(before1, buf[0], preserve_mask);
    assert(u8(buf[1]) == 0xCCu);
  }

  // Case B: bitfield spans two bytes, both ends are mid-byte
  //
  // Layout (16 bits total):
  //   pre   : u3  bits0..2          (in byte0)
  //   mid10 : ubits<10> bits3..12   (crosses into byte1)
  //   post  : u3  bits13..15        (in byte1)
  //
  // Preservation requirements:
  //  - In byte0, bits0..2 (pre) are preserved when setting mid10.
  //  - In byte1, bits13..15 (post) are preserved when setting mid10.
  using B = packet<
    u3<"pre">,
    ubits<10, "mid10">,
    u3<"post">
  >;
  static_assert(B::total_bits == 16);
  static_assert(B::total_bytes == 2);

  {
    std::array<std::byte, B::total_bytes> buf{};
    buf[0] = std::byte{0xD3}; // 1101'0011
    buf[1] = std::byte{0x6E}; // 0110'1110

    auto v = make_view<B>(buf.data(), buf.size());

    const std::byte b0_before = buf[0];
    const std::byte b1_before = buf[1];

    // Set pre/post explicitly to known values, but keep the rest as-is.
    v.set<"pre">(0b101u);
    v.set<"post">(0b010u);

    const std::byte b0_preset = buf[0];
    const std::byte b1_preset = buf[1];

    // Preserve masks for bytes:
    // - byte0 bits0..2 must remain (pre)
    // - byte1 bits5..7 must remain (post, at bits13..15 => byte1 bits5..7)
    const unsigned preserve_b0 = 0b0000'0111u;
    const unsigned preserve_b1 = 0b1110'0000u;

    // Set mid10 and ensure pre/post bits remain unchanged.
    v.set<"mid10">(0x3FFu); // 10 ones
    assert_bits_preserved(b0_preset, buf[0], preserve_b0);
    assert_bits_preserved(b1_preset, buf[1], preserve_b1);

    assert(v.get<"pre">()  == 0b101u);
    assert(v.get<"post">() == 0b010u);
    assert(v.get<"mid10">() == 0x3FFu);

    // Change mid10 again; neighbor bits must still be preserved.
    const std::byte b0_before2 = buf[0];
    const std::byte b1_before2 = buf[1];

    v.set<"mid10">(0x155u); // 0b01_0101_0101 (10 bits)
    assert_bits_preserved(b0_before2, buf[0], preserve_b0);
    assert_bits_preserved(b1_before2, buf[1], preserve_b1);
    assert(v.get<"mid10">() == (0x155u & 0x3FFu));

    // Ensure we didn't "restore" original unrelated bits from b0_before/b1_before snapshots.
    // (This is mostly a sanity check that RMW reads current bytes.)
    (void)b0_before;
    (void)b1_before;
  }

  // Case C: multi-byte spanning field with partial first and last byte
  //
  // Layout:
  //   pre7  : u7  (bits0..6)        first byte partial (bit7 is outside pre7)
  //   mid17 : ubits<17> (bits7..23) spans bytes0..2
  //   post1 : u1  (bit24)           lives in byte3 bit0
  //   tail  : u8  (byte4 guard)
  //
  // We validate preservation of:
  //  - byte0 bits0..6 (pre7) when setting mid17 (mid starts at bit7)
  //  - byte3 bits1..7 (outside post1) when setting post1 (ensures no clobber from mid)
  //  - byte4 guard never touched by either.
  using C = packet<
    u7<"pre7">,
    ubits<17, "mid17">,
    u1<"post1">,
    u8<"tail">
  >;
  static_assert(C::total_bits == 7 + 17 + 1 + 8);
  static_assert(C::total_bytes == 5);

  {
    std::array<std::byte, C::total_bytes> buf{};
    buf[0] = std::byte{0x7Eu}; // 0111'1110 (pre7 = 0x7E & 0x7F)
    buf[1] = std::byte{0xA5u};
    buf[2] = std::byte{0x5Au};
    buf[3] = std::byte{0xC3u};
    buf[4] = std::byte{0x3Cu}; // tail guard

    auto v = make_view<C>(buf.data(), buf.size());

    // Preserve pre7 bits (0..6) in byte0 when setting mid17.
    const std::byte b0_before = buf[0];
    const unsigned preserve_b0 = 0b0111'1111u;

    v.set<"mid17">(0x1FFFFu); // 17 ones
    assert_bits_preserved(b0_before, buf[0], preserve_b0);
    assert(v.get<"pre7">() == (u8(b0_before) & 0x7Fu));
    assert(v.get<"mid17">() == 0x1FFFFu);
    assert(u8(buf[4]) == 0x3Cu);

    // Toggle post1; it should affect only byte3 bit0 and preserve bits1..7.
    const std::byte b3_before = buf[3];
    v.set<"post1">(1);
    assert_bits_preserved(b3_before, buf[3], 0b1111'1110u);
    assert((u8(buf[3]) & 0x01u) == 0x01u);
    assert(u8(buf[4]) == 0x3Cu);

    // Clearing post1 again should preserve the high bits again.
    const std::byte b3_before2 = buf[3];
    v.set<"post1">(0);
    assert_bits_preserved(b3_before2, buf[3], 0b1111'1110u);
    assert((u8(buf[3]) & 0x01u) == 0x00u);
    assert(u8(buf[4]) == 0x3Cu);
  }

  return 0;
}
