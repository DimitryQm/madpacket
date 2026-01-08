#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/layout/subpacket_offsets_golden.cpp
//
// Layout Model (§9):
//   A subpacket occupies exactly SubPacket::total_bits in the parent packing stream.
//   The nested view returned by get<SubName>() has base pointer:
//       parent_base + (parent_subpacket_bit_offset >> 3)
//   and nested field absolute bit positions compose as:
//       parent_subpacket_bit_offset + nested_field_bit_offset
//
// This file provides two “golden” validations:
//   (1) single-level subpacket with intra-byte bitfields
//   (2) two-level nested subpacket composition
//
// We also assert Packet::offsets_bits / sizes_bits for the relevant fields.

static inline unsigned byte_u(std::byte b) { return std::to_integer<unsigned>(b); }
static inline std::byte b8(unsigned v) { return std::byte{static_cast<unsigned char>(v & 0xFFu)}; }

static inline void zero(std::byte* p, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) p[i] = b8(0);
}

int main() {
  using namespace mad;

  // (1) Single-level: intra-byte bitfields inside the subpacket
  using Sub = packet<
    ubits<3, "a">,
    ubits<5, "b">,
    u8<"c">
  >;
  static_assert(Sub::total_bits == 16);
  static_assert(Sub::total_bits % 8 == 0);
  static_assert(Sub::total_bytes == 2);

  using P = packet<
    u8<"pre">,
    subpacket<Sub, "sub">,
    u8<"post">
  >;
  static_assert(P::total_bytes == 4);

  // Compile-time offsets/sizes (exact layout facts)
  static_assert(P::field_count == 3);
  static_assert(P::offsets_bits[0] == 0);
  static_assert(P::offsets_bits[1] == 8);
  static_assert(P::offsets_bits[2] == 24);
  static_assert(P::sizes_bits[1] == Sub::total_bits);

  static_assert(Sub::field_count == 3);
  static_assert(Sub::offsets_bits[0] == 0);
  static_assert(Sub::offsets_bits[1] == 3);
  static_assert(Sub::offsets_bits[2] == 8);

  std::array<std::byte, P::total_bytes> buf1{};
  zero(buf1.data(), buf1.size());

  auto v1 = make_view<P>(buf1.data(), buf1.size());
  v1.set<"pre">(0xAAu);
  v1.set<"post">(0x55u);

  auto s1 = v1.get<"sub">();
  assert(s1.data() == (buf1.data() + 1));
  assert(s1.size_bytes() == Sub::total_bytes);

  s1.set<"a">(0b101u);      // 5
  s1.set<"b">(0b11001u);    // 25
  s1.set<"c">(0x7Eu);

  // Expected packing:
  //   buf1[0] = pre = 0xAA
  //   buf1[1] = (b<<3)|a = (25<<3)|5 = 0xCD
  //   buf1[2] = c = 0x7E
  //   buf1[3] = post = 0x55
  assert(byte_u(buf1[0]) == 0xAAu);
  assert(byte_u(buf1[1]) == 0xCDu);
  assert(byte_u(buf1[2]) == 0x7Eu);
  assert(byte_u(buf1[3]) == 0x55u);

  // Readback via both nested view and a direct view over sub bytes.
  auto s1_direct = make_view<Sub>(buf1.data() + 1, Sub::total_bytes);
  assert(static_cast<unsigned>(s1.get<"a">()) == 5u);
  assert(static_cast<unsigned>(s1.get<"b">()) == 25u);
  assert(static_cast<unsigned>(s1.get<"c">()) == 0x7Eu);
  assert(static_cast<unsigned>(s1_direct.get<"a">()) == 5u);
  assert(static_cast<unsigned>(s1_direct.get<"b">()) == 25u);
  assert(static_cast<unsigned>(s1_direct.get<"c">()) == 0x7Eu);

  // Sweep a/b to catch offset composition bugs.
  for (unsigned a = 0; a < 8; ++a) {
    for (unsigned bb = 0; bb < 32; bb += 7) {
      s1.set<"a">(a);
      s1.set<"b">(bb);
      const unsigned expect = ((bb & 0x1Fu) << 3) | (a & 0x7u);
      assert(byte_u(buf1[1]) == expect);
      assert(static_cast<unsigned>(s1.get<"a">()) == (a & 0x7u));
      assert(static_cast<unsigned>(s1.get<"b">()) == (bb & 0x1Fu));
      assert(byte_u(buf1[0]) == 0xAAu);
      assert(byte_u(buf1[3]) == 0x55u);
    }
  }

  // (2) Two-level nesting: Parent -> Sub2 -> Inner
  using Inner = packet<
    ubits<4, "x">,
    ubits<4, "y">,
    u8<"z">
  >;
  static_assert(Inner::total_bits == 16);
  static_assert(Inner::total_bytes == 2);

  using Sub2 = packet<
    u8<"pfx">,
    subpacket<Inner, "inner">,
    u8<"sfx">
  >;
  static_assert(Sub2::total_bits == 32);
  static_assert(Sub2::total_bytes == 4);

  using P2 = packet<
    u8<"pre">,
    subpacket<Sub2, "sub">,
    u8<"post">
  >;
  static_assert(P2::total_bytes == 6);

  // Offsets: pre=0, sub=8, post=8+32=40
  static_assert(P2::offsets_bits[0] == 0);
  static_assert(P2::offsets_bits[1] == 8);
  static_assert(P2::offsets_bits[2] == 40);

  // Sub2 offsets: pfx=0, inner=8, sfx=24
  static_assert(Sub2::offsets_bits[0] == 0);
  static_assert(Sub2::offsets_bits[1] == 8);
  static_assert(Sub2::offsets_bits[2] == 24);

  // Inner offsets: x=0, y=4, z=8
  static_assert(Inner::offsets_bits[0] == 0);
  static_assert(Inner::offsets_bits[1] == 4);
  static_assert(Inner::offsets_bits[2] == 8);

  std::array<std::byte, P2::total_bytes> buf2{};
  zero(buf2.data(), buf2.size());

  auto v2 = make_view<P2>(buf2.data(), buf2.size());
  v2.set<"pre">(0x10u);
  v2.set<"post">(0x20u);

  auto sub = v2.get<"sub">();
  assert(sub.data() == (buf2.data() + 1));
  sub.set<"pfx">(0xA1u);
  sub.set<"sfx">(0xB2u);

  auto inner = sub.get<"inner">();
  // inner starts at sub base + 1 byte => overall buf2 + 2
  assert(inner.data() == (buf2.data() + 2));
  inner.set<"x">(0xDu);   // 13
  inner.set<"y">(0x3u);   // 3
  inner.set<"z">(0x7Fu);

  // Expected bytes:
  // buf2[0] = pre = 0x10
  // buf2[1] = sub.pfx = 0xA1
  // buf2[2] = inner byte0 = (y<<4)|x = (3<<4)|13 = 0x3D
  // buf2[3] = inner byte1 = z = 0x7F
  // buf2[4] = sub.sfx = 0xB2
  // buf2[5] = post = 0x20
  assert(byte_u(buf2[0]) == 0x10u);
  assert(byte_u(buf2[1]) == 0xA1u);
  assert(byte_u(buf2[2]) == 0x3Du);
  assert(byte_u(buf2[3]) == 0x7Fu);
  assert(byte_u(buf2[4]) == 0xB2u);
  assert(byte_u(buf2[5]) == 0x20u);

  // Absolute composition sanity: read back through all layers.
  assert(static_cast<unsigned>(v2.get<"pre">()) == 0x10u);
  assert(static_cast<unsigned>(v2.get<"post">()) == 0x20u);
  assert(static_cast<unsigned>(sub.get<"pfx">()) == 0xA1u);
  assert(static_cast<unsigned>(sub.get<"sfx">()) == 0xB2u);
  assert(static_cast<unsigned>(inner.get<"x">()) == 0xDu);
  assert(static_cast<unsigned>(inner.get<"y">()) == 0x3u);
  assert(static_cast<unsigned>(inner.get<"z">()) == 0x7Fu);

  // A small sweep to ensure (y<<4)|x packing is stable.
  for (unsigned x = 0; x < 16; x += 5) {
    for (unsigned y = 0; y < 16; y += 7) {
      inner.set<"x">(x);
      inner.set<"y">(y);
      const unsigned expect = ((y & 0xFu) << 4) | (x & 0xFu);
      assert(byte_u(buf2[2]) == expect);
      assert(static_cast<unsigned>(inner.get<"x">()) == (x & 0xFu));
      assert(static_cast<unsigned>(inner.get<"y">()) == (y & 0xFu));
      // Guards remain stable
      assert(byte_u(buf2[0]) == 0x10u);
      assert(byte_u(buf2[1]) == 0xA1u);
      assert(byte_u(buf2[4]) == 0xB2u);
      assert(byte_u(buf2[5]) == 0x20u);
    }
  }

  return 0;
}
