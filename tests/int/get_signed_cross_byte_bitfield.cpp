#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// Golden tests for signed get() on bitfield integer fields that cross byte boundaries.
// This locks down:
// - le-stream bit numbering for signed bitfields
// - two's complement sign extension from arbitrary bit widths
// - interaction with neighboring fields (no bleed)

// Packet used for golden-byte checks:
//   head: u3 at bits [0..2]
//   s11:  ibits<11> at bits [3..13] (crosses byte boundary)
//   tail: u2 at bits [14..15]

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  using P = packet<
    u3<"head">,
    ibits<11, "s11">,
    u2<"tail">
  >;

  static_assert(P::total_bits == 16);
  static_assert(P::total_bytes == 2);

  {
    std::array<std::byte, P::total_bytes> buf{std::byte{0}, std::byte{0}};
    auto v = make_view<P>(buf.data(), buf.size());

    // Ensure signed get() returns int64_t for signed fields.
    static_assert(std::is_same_v<decltype(v.get<"s11">()), std::int64_t>);
    static_assert(std::is_same_v<decltype(v.get<"head">()), std::uint64_t>);
    static_assert(std::is_same_v<decltype(v.get<"tail">()), std::uint64_t>);

    // ---- Golden 1: s11 = 0x400 (only sign bit set) => -1024
    // Choose head=0b101 (5) and tail=0b10 (2) to ensure neighbors don't interfere.
    v.set<"head">(5);
    v.set<"tail">(2);
    v.set<"s11">(0x400u);

    // Expected bytes (derived from contract bit numbering):
    // bits0..2 head=101 => byte0 low bits = 0x05
    // bit13 set (sign bit), bit15 set (tail bit1), others clear => byte1 = 0xA0
    assert(u8(buf[0]) == 0x05u);
    assert(u8(buf[1]) == 0xA0u);

    assert(v.get<"head">() == 5u);
    assert(v.get<"tail">() == 2u);
    assert(v.get<"s11">() == static_cast<std::int64_t>(-1024));

    // ---- Golden 2: s11 = all ones (0x7FF) => -1 (with head=tail=0)
    v.set<"head">(0);
    v.set<"tail">(0);
    v.set<"s11">(-1);

    // s11 occupies bits3..13; all ones => byte0 bits3..7 = 1 => 0xF8, byte1 bits0..5=1 => 0x3F
    assert(u8(buf[0]) == 0xF8u);
    assert(u8(buf[1]) == 0x3Fu);
    assert(v.get<"s11">() == static_cast<std::int64_t>(-1));

    // ---- Golden 3: s11 = +1023 (0x3FF) => +1023 (sign bit clear)
    v.set<"s11">(0x3FFu);
    assert(u8(buf[0]) == 0xF8u);
    assert(u8(buf[1]) == 0x1Fu);
    assert(v.get<"s11">() == static_cast<std::int64_t>(1023));

    // ---- Neighbor isolation: toggling head and tail must not change s11.
    v.set<"s11">(0x155u); // arbitrary within 11 bits
    const auto s_before = v.get<"s11">();
    v.set<"head">(7);
    v.set<"tail">(3);
    assert(v.get<"s11">() == s_before);

    // And toggling s11 must not change head/tail.
    const auto h_before = v.get<"head">();
    const auto t_before = v.get<"tail">();
    v.set<"s11">(0x2AAu);
    assert(v.get<"head">() == h_before);
    assert(v.get<"tail">() == t_before);
  }

  // Additional cross-byte signed bitfield case: 17-bit signed starting at bit 5.
  // Range -65536..65535. This spans 3 bytes.
  using Q = packet<
    u5<"pfx">,
    ibits<17, "s17">,
    u2<"sfx">
  >;

  static_assert(Q::total_bits == 5 + 17 + 2);
  static_assert(Q::total_bytes == 3);

  {
    std::array<std::byte, Q::total_bytes> buf{std::byte{0}, std::byte{0}, std::byte{0}};
    auto v = make_view<Q>(buf.data(), buf.size());

    // Store sign bit only (1<<16) => -65536
    v.set<"pfx">(0);
    v.set<"sfx">(0);
    v.set<"s17">(1u << 16);
    assert(v.get<"s17">() == static_cast<std::int64_t>(-65536));

    // Store all ones => -1
    v.set<"s17">(-1);
    assert(v.get<"s17">() == static_cast<std::int64_t>(-1));

    // Store max positive 65535 => 0xFFFF
    v.set<"s17">(0xFFFFu);
    assert(v.get<"s17">() == static_cast<std::int64_t>(65535));
  }

  return 0;
}
