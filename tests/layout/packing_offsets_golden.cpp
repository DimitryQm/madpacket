#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// Golden tests for layout packing and offsets.
//
// targets:
// - No implicit padding: offsets are cumulative sum of preceding bits.
// - pad_bits<N> is exactly N bits, pad_bytes<N> is N*8 bits.
// - total_bits is sum of Field::bits, total_bytes is ceil(total_bits/8).
// - offsets_bits[i] and sizes_bits[i] reflect declared order.
//
// This test intentionally uses multiple packet shapes, including:
// - sub-byte fields, mixed with byte-aligned scalars
// - explicit bit and byte padding
// - nested subpackets (byte-aligned by construction)

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  // Case A: simple cumulative offsets
  using A = packet<
    u1<"a0">,     // bit 0
    u7<"a1">,     // bit 1
    u8<"a2">,     // bit 8 (byte-aligned scalar)
    u16<"a3">     // bit 16 (byte-aligned scalar)
  >;

  static_assert(A::total_bits == 1 + 7 + 8 + 16);
  static_assert(A::total_bytes == 4);

  static_assert(A::offsets_bits[0] == 0);
  static_assert(A::offsets_bits[1] == 1);
  static_assert(A::offsets_bits[2] == 8);
  static_assert(A::offsets_bits[3] == 16);

  static_assert(A::sizes_bits[0] == 1);
  static_assert(A::sizes_bits[1] == 7);
  static_assert(A::sizes_bits[2] == 8);
  static_assert(A::sizes_bits[3] == 16);

  {
    std::array<std::byte, A::total_bytes> buf{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    auto v = make_view<A>(buf.data(), buf.size());

    // Write values and check byte-level expectations.
    // a0 at bit0 (LSB of byte0)
    v.set<"a0">(1);
    assert(u8(buf[0]) == 0x01u);

    // a1 is 7 bits starting at bit1; set it to all ones => bits1..7 set.
    v.set<"a1">(0x7Fu);
    assert(u8(buf[0]) == 0xFFu);

    // a2 is byte-aligned at byte1
    v.set<"a2">(0xAAu);
    assert(u8(buf[1]) == 0xAAu);

    // a3 is u16 at byte2 (native endian), so validate via get roundtrip
    v.set<"a3">(0xBEEFu);
    assert(v.get<"a3">() == 0xBEEFu);
  }

  // Case B: explicit pad_bits / pad_bytes and ceil(total_bits/8)
  // Layout:
  //   h (3 bits) @0
  //   pad_bits<5> => next is byte-aligned
  //   x (u16) @8
  //   pad_bytes<1> => 8 bits
  //   y (4 bits) @32
  //   z (ubits<9>) @36 (spans bytes)
  // Total bits = 3 + 5 + 16 + 8 + 4 + 9 = 45 => total_bytes = 6
  using B = packet<
    u3<"h">,
    pad_bits<5>,
    u16<"x">,
    pad_bytes<1>,
    u4<"y">,
    ubits<9, "z">
  >;

  static_assert(B::total_bits == 45);
  static_assert(B::total_bytes == 6);

  static_assert(B::offsets_bits[0] == 0);   // h
  static_assert(B::offsets_bits[1] == 3);   // pad_bits<5>
  static_assert(B::offsets_bits[2] == 8);   // x
  static_assert(B::offsets_bits[3] == 24);  // pad_bytes<1>
  static_assert(B::offsets_bits[4] == 32);  // y
  static_assert(B::offsets_bits[5] == 36);  // z

  static_assert(B::sizes_bits[1] == 5);
  static_assert(B::sizes_bits[3] == 8);
  static_assert(B::sizes_bits[5] == 9);

  {
    std::array<std::byte, B::total_bytes> buf{};
    auto v = make_view<B>(buf.data(), buf.size());

    // Set h = 0b101 -> affects bits0..2 of byte0 => 0x05
    v.set<"h">(5);
    assert((u8(buf[0]) & 0x07u) == 0x05u);

    // x at byte1..2 (native endian), validate roundtrip
    v.set<"x">(0x1234u);
    assert(v.get<"x">() == 0x1234u);

    // y is 4 bits at bit32 => low nibble of byte4 (since 32/8 = 4)
    v.set<"y">(0xAu);
    assert((u8(buf[4]) & 0x0Fu) == 0x0Au);

    // z is 9 bits starting at bit36 => bit4 of byte4 through bit4 of byte5.
    // Write z = 0x1FF => all ones for 9 bits.
    v.set<"z">(0x1FFu);

    // After z=all ones:
    // byte4 bits4..7 should become ones => high nibble = 0xF0, low nibble keeps y=0xA.
    assert(u8(buf[4]) == 0xFAu);
    // byte5 bit0 should become 1 (the 9th bit), other bits 0 => 0x01
    assert(u8(buf[5]) == 0x01u);

    // Read-back checks
    assert(v.get<"h">() == 5u);
    assert(v.get<"y">() == 0xAu);
    assert(v.get<"z">() == 0x1FFu);
  }

  // Case C: subpacket is byte-aligned by explicit padding
  using Sub = packet<
    u8<"sx">,
    u8<"sy">
  >;

  // Outer: u4 + pad_bits<4> => subpacket starts at byte1
  using C = packet<
    u4<"pre">,
    pad_bits<4>,
    subpacket<Sub, "sub">,
    be_u16<"post">
  >;

  static_assert(Sub::total_bytes == 2);
  static_assert(C::total_bytes == 1 + 2 + 2);

  static_assert(C::offsets_bits[0] == 0);   // pre
  static_assert(C::offsets_bits[1] == 4);   // pad_bits<4>
  static_assert(C::offsets_bits[2] == 8);   // sub starts here
  static_assert(C::offsets_bits[3] == 24);  // post starts after sub (16 bits)

  {
    std::array<std::byte, C::total_bytes> buf{};
    auto v = make_view<C>(buf.data(), buf.size());

    v.set<"pre">(0xFu);
    assert((u8(buf[0]) & 0x0Fu) == 0x0Fu);

    auto s = v.get<"sub">();
    s.set<"sx">(0x12u);
    s.set<"sy">(0x34u);

    // sub bytes should be at buf[1] and buf[2]
    assert(u8(buf[1]) == 0x12u);
    assert(u8(buf[2]) == 0x34u);

    // post is big-endian u16 at buf[3..4]
    v.set<"post">(0xBEEFu);
    assert(u8(buf[3]) == 0xBEu);
    assert(u8(buf[4]) == 0xEFu);
    assert(v.get<"post">() == 0xBEEFu);
  }

  return 0;
}
