#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

// tests/mmio_xview/native_exact_uses_scalar_width.cpp
//
// Contract (§12):
//   For byte-aligned scalar integer fields in xview:
//     If Cfg::width == width_policy::native AND the exact field width is permitted
//     by Cfg::read_mask/write_mask, then the implementation uses the base MMIO scalar
//     load/store path (mmio_load_pod/mmio_store_pod) which MAY use typed volatile access
//     of exactly that width when alignment permits and MADPACKET_STRICT_MMIO is not defined.
//
// We cannot portably "prove" the compiler emitted a single typed volatile load/store without
// inspecting assembly. What we can test rigorously is:
//   1) The compile-time width selection chooses the exact field width for native policy.
//   2) The runtime observable semantics match exactly (endianness, truncation, etc).
//   3) We keep alignment conditions satisfied so the implementation is allowed to use typed access.
//
// This test therefore validates width *selection* and correct scalar semantics under xview.

#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::align == 4);

  // Layout:
  //   a16   : bytes 0..1 (LE)
  //   pad16 : bytes 2..3
  //   b32   : bytes 4..7 (BE)
  //   tail  : byte 8
  using P = mad::packet<
    mad::le_u16<"a16">,
    mad::pad_bytes<2>,
    mad::be_u32<"b32">,
    mad::u8<"tail">
  >;
  static_assert(P::total_bytes == 9);

  using Cfg = mad::reg::cfg<Bus, Bus::align, mad::reg::width_policy::native, mad::reg::align_policy::unchecked, mad::reg::caps_all>;

  // Compile-time width selection should choose exact widths (2 and 4) within bus_bytes=4.
  static_assert(mad::reg::detail2::choose_width<mad::reg::width_policy::native>(2, /*off*/0, Bus::bytes, Cfg::read_mask) == 2);
  static_assert(mad::reg::detail2::choose_width<mad::reg::width_policy::native>(2, /*off*/0, Bus::bytes, Cfg::write_mask) == 2);
  static_assert(mad::reg::detail2::choose_width<mad::reg::width_policy::native>(4, /*off*/4, Bus::bytes, Cfg::read_mask) == 4);
  static_assert(mad::reg::detail2::choose_width<mad::reg::width_policy::native>(4, /*off*/4, Bus::bytes, Cfg::write_mask) == 4);

  // Layout estimator: worst_case_transactions for these fields should be 1 under native policy.
  static_assert(mad::reg::layout_info<P, Cfg>::template worst_case_transactions<0>() == 1);
  static_assert(mad::reg::layout_info<P, Cfg>::template worst_case_transactions<2>() == 1);

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  for (std::size_t i = 0; i < mem.size(); ++i) mem[i] = std::byte{0};

  auto vx = mad::reg::make_xview<P, Cfg>(reinterpret_cast<volatile void*>(mem.data()));
  auto vcx = mad::reg::make_xcview<P, Cfg>(reinterpret_cast<volatile void const*>(mem.data()));
  (void)vcx;

  // Write and verify byte layout
  // -----------------------------
  vx.set<"a16">(0x1234u);
  vx.set<"b32">(0x11223344u);
  vx.set<"tail">(0xABu);

  // le_u16 stores 34 12
  assert(u8(mem[0]) == 0x34u);
  assert(u8(mem[1]) == 0x12u);

  // pad bytes remain zero
  assert(u8(mem[2]) == 0x00u);
  assert(u8(mem[3]) == 0x00u);

  // be_u32 stores 11 22 33 44
  assert(u8(mem[4]) == 0x11u);
  assert(u8(mem[5]) == 0x22u);
  assert(u8(mem[6]) == 0x33u);
  assert(u8(mem[7]) == 0x44u);

  assert(u8(mem[8]) == 0xABu);

  // Read back values
  assert(vx.get<"a16">() == 0x1234u);
  assert(vx.get<"b32">() == 0x11223344u);
  assert(vx.get<"tail">() == 0xABu);

  // Truncation semantics for scalar fields (still apply in xview):
  // set stores low bits only (mod 2^bits).
  vx.set<"a16">(0x1'2345u); // stores 0x2345
  assert(vx.get<"a16">() == 0x2345u);
  assert(u8(mem[0]) == 0x45u);
  assert(u8(mem[1]) == 0x23u);

  // For be_u32, 64-bit input is truncated to 32 bits.
  vx.set<"b32">(0x1'0000'0001ull); // stores 0x00000001
  assert(vx.get<"b32">() == 0x00000001u);
  assert(u8(mem[4]) == 0x00u);
  assert(u8(mem[5]) == 0x00u);
  assert(u8(mem[6]) == 0x00u);
  assert(u8(mem[7]) == 0x01u);

  // Repeat with multiple patterns to guard against stale caching.
  for (std::uint32_t i = 0; i < 100; ++i) {
    const std::uint16_t a = static_cast<std::uint16_t>(0xB000u + i);
    const std::uint32_t b = 0xA5A50000u ^ (i * 0x10203u);
    const std::uint8_t  t = static_cast<std::uint8_t>(i ^ 0x5Au);

    vx.set<"a16">(a);
    vx.set<"b32">(b);
    vx.set<"tail">(t);

    assert(static_cast<std::uint16_t>(vx.get<"a16">()) == a);
    assert(static_cast<std::uint32_t>(vx.get<"b32">()) == b);
    assert(static_cast<std::uint8_t>(vx.get<"tail">()) == t);
  }

  return 0;
}
