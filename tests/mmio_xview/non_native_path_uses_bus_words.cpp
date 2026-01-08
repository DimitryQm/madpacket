#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

// tests/mmio_xview/non_native_path_uses_bus_words.cpp
//
// Contract (§12):
//   For byte-aligned scalar integer fields in xview:
//     If the exact field width is NOT selected (because width policy is not 'native'
//     or because read/write masks don't permit the exact width), the implementation
//     performs bus-word based access to assemble/store the field bytes.
//     Stores may involve read-modify-write (RMW) to preserve unrelated bytes.
//
// This test forces the "non-native" path by disallowing the exact width.
// We then validate the critical *observable semantic* promise of the bus-word path:
//   - Writing a scalar field that occupies a subregion of a bus word preserves the other bytes.
//   - The write works even when the field spans two bus words.
//   - Endianness tags still define the in-memory byte order of the field.

#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::align == 4);

  // Force promotion: allow ONLY 4-byte transactions for both reads and writes.
  using Caps4Only = mad::reg::caps<mad::reg::mask_for_bytes(4), mad::reg::mask_for_bytes(4)>;

  // Use width_policy::native but disallow exact 2-byte width; choose_width should promote to 4.
  using CfgPromote = mad::reg::cfg<Bus, Bus::align, mad::reg::width_policy::native, mad::reg::align_policy::unchecked, Caps4Only>;

  // Packet designed to exercise:
  //   - a16_be: 16-bit scalar at byte offset 3 (spans word0->word1)
  //   - guard bytes around it to detect accidental clobber
  using P = mad::packet<
    mad::u8<"g0">,
    mad::u8<"g1">,
    mad::u8<"g2">,
    mad::be_u16<"a16_be">,   // starts at byte 3 (crosses bus32 word boundary)
    mad::u8<"g5">,
    mad::u8<"g6">,
    mad::u8<"g7">
  >;
  static_assert(P::total_bytes == 8);

  // Compile-time selection: region_bytes=2, bus_bytes=4, mask has no 2 => promote to 4.
  static_assert(mad::reg::detail2::choose_width<mad::reg::width_policy::native>(2, /*off*/3, Bus::bytes, CfgPromote::write_mask) == 4);
  static_assert(mad::reg::detail2::choose_width<mad::reg::width_policy::native>(2, /*off*/3, Bus::bytes, CfgPromote::read_mask) == 4);

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  // Initialize with nontrivial pattern so clobbers show up.
  mem[0] = std::byte{0x10}; // g0
  mem[1] = std::byte{0x21}; // g1
  mem[2] = std::byte{0x32}; // g2
  mem[3] = std::byte{0x43}; // (will be overwritten by a16_be hi)
  mem[4] = std::byte{0x54}; // (will be overwritten by a16_be lo)
  mem[5] = std::byte{0x65}; // g5
  mem[6] = std::byte{0x76}; // g6
  mem[7] = std::byte{0x87}; // g7

  auto vx = mad::reg::make_xview<P, CfgPromote>(reinterpret_cast<volatile void*>(mem.data()));

  // Preserve guards before the write.
  const unsigned g0 = u8(mem[0]);
  const unsigned g1 = u8(mem[1]);
  const unsigned g2 = u8(mem[2]);
  const unsigned g5 = u8(mem[5]);
  const unsigned g6 = u8(mem[6]);
  const unsigned g7 = u8(mem[7]);

  // Perform the spanning write: big-endian value 0xABCD stores [3]=0xAB, [4]=0xCD.
  vx.set<"a16_be">(0xABCDu);

  // Field bytes updated correctly:
  assert(u8(mem[3]) == 0xABu);
  assert(u8(mem[4]) == 0xCDu);

  // Guards must be preserved (RMW preservation semantic).
  assert(u8(mem[0]) == g0);
  assert(u8(mem[1]) == g1);
  assert(u8(mem[2]) == g2);
  assert(u8(mem[5]) == g5);
  assert(u8(mem[6]) == g6);
  assert(u8(mem[7]) == g7);

  // Read back yields same logical value.
  assert(vx.get<"a16_be">() == 0xABCDu);

  // Hammer a sequence of values to catch off-by-one in spanning logic.
  for (std::uint32_t i = 0; i < 256; ++i) {
    // Reset guards each iteration to a changing pattern.
    mem[0] = std::byte{static_cast<unsigned char>(0x10u ^ i)};
    mem[1] = std::byte{static_cast<unsigned char>(0x20u + (i & 0x0Fu))};
    mem[2] = std::byte{static_cast<unsigned char>(0x30u + (i >> 4))};
    mem[5] = std::byte{static_cast<unsigned char>(0x60u ^ (i * 3u))};
    mem[6] = std::byte{static_cast<unsigned char>(0x70u ^ (i * 5u))};
    mem[7] = std::byte{static_cast<unsigned char>(0x80u ^ (i * 7u))};

    const std::uint16_t v = static_cast<std::uint16_t>((i << 8) | (i ^ 0x5Au));
    vx.set<"a16_be">(v);

    // Check bytes (big endian).
    assert(u8(mem[3]) == static_cast<unsigned>((v >> 8) & 0xFFu));
    assert(u8(mem[4]) == static_cast<unsigned>((v >> 0) & 0xFFu));

    // Guards preserved.
    assert(u8(mem[0]) == (0x10u ^ i));
    assert(u8(mem[1]) == (0x20u + (i & 0x0Fu)));
    assert(u8(mem[2]) == (0x30u + (i >> 4)));
    assert(u8(mem[5]) == static_cast<unsigned>(0x60u ^ (i * 3u)));
    assert(u8(mem[6]) == static_cast<unsigned>(0x70u ^ (i * 5u)));
    assert(u8(mem[7]) == static_cast<unsigned>(0x80u ^ (i * 7u)));

    // Read back.
    assert(static_cast<std::uint16_t>(vx.get<"a16_be">()) == v);
  }

  return 0;
}
