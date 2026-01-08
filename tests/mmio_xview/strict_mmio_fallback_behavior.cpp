#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

// tests/mmio_xview/strict_mmio_fallback_behavior.cpp
//
// Contract (§13):
//   If MADPACKET_STRICT_MMIO is defined, the implementation disables certain typed volatile
//   load/store fast paths and may fall back to bytewise volatile operations even when the
//   configuration expresses a desire for bus-word transactions.
//
// What we can validate portably:
//   - Under strict mode, xview still preserves the semantic contract for:
//       * scalar reads/writes (endianness, truncation)
//       * bus-word bitfield one-word RMW semantics (bit numbering + preserve outside bits)
//       * promoted scalar writes via bus-word assembly/RMW semantics
//
// What we cannot validate portably without inspecting assembly or real bus traces:
//   - that the compiler emitted bytewise accesses vs typed volatile transactions.
//
// This file therefore focuses on semantic correctness under strict mode in scenarios that
// would otherwise be eligible for typed access.

#define MADPACKET_STRICT_MMIO 1
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

  // 1) Scalar correctness under strict mode (endianness + truncation).
  using PScalar = mad::packet<
    mad::le_u16<"a16">,      // bytes 0..1
    mad::pad_bytes<2>,       // bytes 2..3
    mad::be_u32<"b32">,      // bytes 4..7
    mad::u8<"tail">          // byte 8
  >;
  static_assert(PScalar::total_bytes == 9);

  using CfgNative = mad::reg::cfg<Bus, Bus::align, mad::reg::width_policy::native,
                                  mad::reg::align_policy::unchecked, mad::reg::caps_all>;

  alignas(Bus::align) std::array<std::byte, PScalar::total_bytes> mem_s{};
  for (auto& b : mem_s) b = std::byte{0};

  auto vx_s = mad::reg::make_xview<PScalar, CfgNative>(reinterpret_cast<volatile void*>(mem_s.data()));

  vx_s.set<"a16">(0x1234u);
  vx_s.set<"b32">(0x11223344u);
  vx_s.set<"tail">(0xABu);

  assert(u8(mem_s[0]) == 0x34u);
  assert(u8(mem_s[1]) == 0x12u);
  assert(u8(mem_s[4]) == 0x11u);
  assert(u8(mem_s[7]) == 0x44u);

  assert(vx_s.get<"a16">() == 0x1234u);
  assert(vx_s.get<"b32">() == 0x11223344u);
  assert(vx_s.get<"tail">() == 0xABu);

  // truncation still holds
  vx_s.set<"a16">(0x1'2345u);
  assert(vx_s.get<"a16">() == 0x2345u);

  // 2) One-word bitfield bus RMW semantics under strict mode.
  using PBF = mad::packet<
    mad::pad_bits<5>,
    mad::ubits<10, "bf10">,
    mad::pad_bits<17>,
    mad::u32<"word1">
  >;
  static_assert(PBF::total_bytes == 8);

  using CfgBus = mad::reg::cfg_enforce_bus<Bus, Bus::align>;

  alignas(Bus::align) std::array<std::byte, PBF::total_bytes> mem_bf{};
  store_le32(mem_bf.data() + 0, 0xDDBBCCAAu);
  store_le32(mem_bf.data() + 4, 0x11223344u);

  auto vx_bf = mad::reg::make_xview<PBF, CfgBus>(reinterpret_cast<volatile void*>(mem_bf.data()));

  const std::uint32_t w0_before = le32_from_bytes(mem_bf.data());
  const std::uint32_t mask10 = (1u << 10) - 1u;
  const std::uint32_t m = (mask10 << 5);

  vx_bf.set<"bf10">(0x155u);

  const std::uint32_t w0_after = le32_from_bytes(mem_bf.data());
  const std::uint32_t expected = (w0_before & ~m) | ((0x155u & mask10) << 5);

  assert(w0_after == expected);
  assert((w0_after & ~m) == (w0_before & ~m));
  assert(le32_from_bytes(mem_bf.data() + 4) == 0x11223344u);
  assert(static_cast<std::uint32_t>(vx_bf.get<"bf10">()) == (0x155u & mask10));

  // 3) Promote scalar to bus-word operations (non-native path) and validate RMW preservation.
  //    We force promotion by allowing only 4-byte accesses; writing a 16-bit field at offset 3
  //    spans two bus words and requires bus-word assembly/RMW.
  using Caps4Only = mad::reg::caps<mad::reg::mask_for_bytes(4), mad::reg::mask_for_bytes(4)>;
  using CfgPromote = mad::reg::cfg<Bus, Bus::align, mad::reg::width_policy::native,
                                  mad::reg::align_policy::unchecked, Caps4Only>;

  using PPromote = mad::packet<
    mad::u8<"g0">, mad::u8<"g1">, mad::u8<"g2">,
    mad::be_u16<"a16_be">, // starts at byte 3 (cross boundary)
    mad::u8<"g5">, mad::u8<"g6">, mad::u8<"g7">
  >;
  static_assert(PPromote::total_bytes == 8);

  alignas(Bus::align) std::array<std::byte, PPromote::total_bytes> mem_p{};
  mem_p[0]=std::byte{0x10}; mem_p[1]=std::byte{0x21}; mem_p[2]=std::byte{0x32};
  mem_p[3]=std::byte{0x43}; mem_p[4]=std::byte{0x54};
  mem_p[5]=std::byte{0x65}; mem_p[6]=std::byte{0x76}; mem_p[7]=std::byte{0x87};

  auto vx_p = mad::reg::make_xview<PPromote, CfgPromote>(reinterpret_cast<volatile void*>(mem_p.data()));

  const unsigned g0 = u8(mem_p[0]);
  const unsigned g1 = u8(mem_p[1]);
  const unsigned g2 = u8(mem_p[2]);
  const unsigned g5 = u8(mem_p[5]);
  const unsigned g6 = u8(mem_p[6]);
  const unsigned g7 = u8(mem_p[7]);

  vx_p.set<"a16_be">(0xABCDu);

  assert(u8(mem_p[3]) == 0xABu);
  assert(u8(mem_p[4]) == 0xCDu);

  // Guards preserved
  assert(u8(mem_p[0]) == g0);
  assert(u8(mem_p[1]) == g1);
  assert(u8(mem_p[2]) == g2);
  assert(u8(mem_p[5]) == g5);
  assert(u8(mem_p[6]) == g6);
  assert(u8(mem_p[7]) == g7);

  assert(vx_p.get<"a16_be">() == 0xABCDu);

  // Stress small loop
  for (std::uint32_t i = 0; i < 128; ++i) {
    mem_p[0]=std::byte{static_cast<unsigned char>(0x10u ^ i)};
    mem_p[1]=std::byte{static_cast<unsigned char>(0x20u + (i & 0x0Fu))};
    mem_p[2]=std::byte{static_cast<unsigned char>(0x30u + (i >> 4))};
    mem_p[5]=std::byte{static_cast<unsigned char>(0x60u ^ (i * 3u))};
    mem_p[6]=std::byte{static_cast<unsigned char>(0x70u ^ (i * 5u))};
    mem_p[7]=std::byte{static_cast<unsigned char>(0x80u ^ (i * 7u))};

    const std::uint16_t v = static_cast<std::uint16_t>((i << 8) | (i ^ 0x5Au));
    vx_p.set<"a16_be">(v);

    assert(u8(mem_p[3]) == static_cast<unsigned>((v >> 8) & 0xFFu));
    assert(u8(mem_p[4]) == static_cast<unsigned>((v >> 0) & 0xFFu));

    assert(static_cast<std::uint16_t>(vx_p.get<"a16_be">()) == v);
  }

  return 0;
}
