#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/bitfields/window_minimality.cpp
//
// Contract (§7):
//   - For bitfield integer fields, get/set touch only the minimal byte window
//     that contains the field's bit range.
//
// In this header, the *window* is computed by mad::detail::bit_window<BitOffset, BitCount>
// with need_bytes = ceil((shift + BitCount)/8). We validate that this value matches the
// mathematically minimal count across many shapes and boundary cases.
//
// We also run runtime "no unintended modification" checks around the computed window.
// (We cannot observe raw memory reads/writes, but we can observe unintended changes.)

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

template <std::size_t BitOffset, std::size_t BitCount>
constexpr std::size_t minimal_need_bytes_ref() {
  constexpr std::size_t shift = (BitOffset & 7u);
  return (shift + BitCount + 7u) >> 3;
}

int main() {
  using namespace mad;

  // Compile-time window checks
  static_assert(detail::bit_window<0, 1>::need_bytes == 1);
  static_assert(detail::bit_window<0, 8>::need_bytes == 1);
  static_assert(detail::bit_window<0, 9>::need_bytes == 2);

  static_assert(detail::bit_window<1, 1>::need_bytes == 1);
  static_assert(detail::bit_window<1, 7>::need_bytes == 1);
  static_assert(detail::bit_window<1, 8>::need_bytes == 2);

  static_assert(detail::bit_window<7, 1>::need_bytes == 1);
  static_assert(detail::bit_window<7, 2>::need_bytes == 2);
  static_assert(detail::bit_window<7, 9>::need_bytes == 2);

  static_assert(detail::bit_window<8, 1>::need_bytes == 1);
  static_assert(detail::bit_window<8, 64>::need_bytes == 8);

  static_assert(detail::bit_window<15, 1>::need_bytes == 1);
  static_assert(detail::bit_window<15, 2>::need_bytes == 2);

  static_assert(detail::bit_window<63, 1>::need_bytes == 1);
  static_assert(detail::bit_window<63, 2>::need_bytes == 2);

  static_assert(detail::bit_window<5, 17>::need_bytes == minimal_need_bytes_ref<5, 17>());
  static_assert(detail::bit_window<13, 24>::need_bytes == minimal_need_bytes_ref<13, 24>());
  static_assert(detail::bit_window<60, 10>::need_bytes == minimal_need_bytes_ref<60, 10>());
  static_assert(detail::bit_window<61, 64>::need_bytes == minimal_need_bytes_ref<61, 64>()); // need_bytes can be 9

  // A few explicit 9-byte windows (BitCount<=64 but shift can force need_bytes=9):
  static_assert(detail::bit_window<1, 64>::need_bytes == 9);
  static_assert(detail::bit_window<7, 64>::need_bytes == 9);

  // Runtime "no unintended modification" checks
  //
  // We set a bitfield inside a buffer with distinctive sentinels and validate:
  //  - bytes strictly outside the computed window remain unchanged
  //  - bytes inside the window change only as expected via named field reads/writes

  // Helper lambda for verifying that bytes outside [start, start+len) remain unchanged.
  auto assert_outside_unchanged = [](auto const& before, auto const& after, std::size_t start, std::size_t len) {
    for (std::size_t i = 0; i < before.size(); ++i) {
      const bool inside = (i >= start) && (i < (start + len));
      if (!inside) {
        assert(before[i] == after[i]);
      }
    }
  };

  // Case A: BitOffset=3, BitCount=10 => shift 3, need_bytes = 2
  // Field spans bytes 1..2 (because we place it after one full guard byte).
  {
    using P = packet<
      u8<"guard0">,
      pad_bits<3>,
      ubits<10, "bf">,
      pad_bits<3>,
      u8<"guard1">,
      u8<"guard2">
    >;
    static_assert(P::total_bytes == 5);

    std::array<std::byte, P::total_bytes> buf{
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}, std::byte{0x55}
    };
    auto before = buf;

    auto v = make_view<P>(buf.data(), buf.size());
    v.set<"bf">(0x3FFu); // set 10 ones

    // BitOffset of bf is 8+3 = 11, so base byte is 1, shift 3.
    constexpr std::size_t bit_off = 11;
    constexpr std::size_t start_byte = bit_off >> 3; // 1
    constexpr std::size_t need = detail::bit_window<bit_off, 10>::need_bytes; // 2

    assert_outside_unchanged(before, buf, start_byte, need);

    // Guards still readable
    assert(v.get<"guard0">() == 0x11u);
    assert(v.get<"guard1">() == 0x44u);
    assert(v.get<"guard2">() == 0x55u);

    // Field roundtrip
    assert(v.get<"bf">() == 0x3FFu);
  }

  // Case B: BitOffset=7, BitCount=2 => shift 7, need_bytes = 2
  // The smallest cross-byte case; ensures "need_bytes bumps to 2" at boundary.
  {
    using P = packet<
      u8<"g0">,
      pad_bits<7>,
      ubits<2, "b2">,
      u8<"g1">,
      u8<"g2">
    >;
    static_assert(P::total_bytes == 4);

    std::array<std::byte, P::total_bytes> buf{
      std::byte{0xA0}, std::byte{0xB1}, std::byte{0xC2}, std::byte{0xD3}
    };
    auto before = buf;

    auto v = make_view<P>(buf.data(), buf.size());
    v.set<"b2">(0b11u);

    // b2 is at bit_off = 8 + 7 = 15, start_byte=1, need_bytes=2.
    constexpr std::size_t bit_off = 15;
    constexpr std::size_t start_byte = bit_off >> 3; // 1
    constexpr std::size_t need = detail::bit_window<bit_off, 2>::need_bytes; // 2

    assert_outside_unchanged(before, buf, start_byte, need);

    assert(v.get<"g0">() == 0xA0u);
    assert(v.get<"g1">() == 0xC2u);
    assert(v.get<"g2">() == 0xD3u);
    assert(v.get<"b2">() == 0b11u);
  }

  // Case C: BitOffset=1, BitCount=64 => shift 1, need_bytes=9
  // This stresses the "9-byte window" path. We surround it with guards.
  {
    using P = packet<
      u8<"g0">,
      pad_bits<1>,
      ubits<64, "b64">,
      pad_bits<7>,
      u8<"g1">
    >;
    static_assert(P::total_bytes == 1 + 9 + 1);

    std::array<std::byte, P::total_bytes> buf{};
    // Fill with a linear pattern so unintended modifications are obvious.
    for (std::size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<std::byte>(i * 17u);

    auto before = buf;

    auto v = make_view<P>(buf.data(), buf.size());
    v.set<"b64">(0x0123456789ABCDEFull);

    // b64 bit offset = 8+1 = 9 => start_byte=1, need_bytes=9.
    constexpr std::size_t bit_off = 9;
    constexpr std::size_t start_byte = bit_off >> 3; // 1
    constexpr std::size_t need = detail::bit_window<bit_off, 64>::need_bytes; // 9

    assert_outside_unchanged(before, buf, start_byte, need);

    // Guards preserved
    assert(v.get<"g0">() == u8(before[0]));
    assert(v.get<"g1">() == u8(before[buf.size()-1]));

    // Roundtrip
    assert(v.get<"b64">() == 0x0123456789ABCDEFull);
  }

  return 0;
}
