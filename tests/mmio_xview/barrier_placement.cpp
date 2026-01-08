#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

// tests/mmio_xview/barrier_placement.cpp
//
// Contract (§12):
//   For xview stores, MAD_MMIO_BARRIER() is placed immediately before and immediately
//   after the store sequence. For loads, no barrier is automatically inserted.
//   This applies to:
//     - scalar stores (exact-width path or bus-word path)
//     - bitfield one-word bus RMW
//     - bitfield fallback byte-window store sequence
//
// This test instruments MAD_MMIO_BARRIER and verifies:
//   - get does not call the barrier
//   - set calls it exactly twice per operation (before + after) across multiple store paths
//
// Define MAD_MMIO_BARRIER before including the header.

static inline std::uint64_t g_barriers = 0;
#define MAD_MMIO_BARRIER() do { ++g_barriers; } while(0)

#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline void store_bytes(std::byte* p, std::initializer_list<unsigned> bytes) {
  std::size_t i = 0;
  for (unsigned b : bytes) p[i++] = std::byte{static_cast<unsigned char>(b & 0xFFu)};
}

static inline void reset_barriers() { g_barriers = 0; }

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::bits == 32);
  static_assert(Bus::align == 4);

  // Packet to exercise multiple set paths under xview:
  //   - s16_le at offset 0 => scalar path eligible (exact-width chosen under native caps)
  //   - bf_one in word0 => one-word bus RMW bitfield path
  //   - bf_cross crosses word0->word1 => fallback byte-window bitfield path
  //   - guard byte at end
  using P = mad::packet<
    mad::le_u16<"s16_le">,     // bytes 0..1
    mad::ubits<10, "bf_one">,  // bits 16..25 within word0 (fits)
    mad::pad_bits<2>,          // bits 26..27
    mad::ubits<8, "bf_cross">, // bits 28..35 crosses boundary
    mad::pad_bits<4>,          // bits 36..39
    mad::u8<"guard">           // bits 40..47 (byte 5)
  >;
  static_assert(P::total_bytes == 6);

  using Cfg = mad::reg::cfg<Bus, Bus::align, mad::reg::width_policy::native, mad::reg::align_policy::unchecked, mad::reg::caps_all>;

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  store_bytes(mem.data(), {0x10, 0x32, 0x54, 0x76, 0x98, 0xAA});

  auto vx = mad::reg::make_xview<P, Cfg>(reinterpret_cast<volatile void*>(mem.data()));

  // get must not invoke barrier
  reset_barriers();
  (void)vx.get<"s16_le">();
  (void)vx.get<"bf_one">();
  (void)vx.get<"bf_cross">();
  (void)vx.get<"guard">();
  assert(g_barriers == 0);

  // scalar set => exactly 2 barriers
  reset_barriers();
  vx.set<"s16_le">(0xBEEFu);
  assert(g_barriers == 2);

  // check bytes: le_u16 => EF BE
  assert(u8(mem[0]) == 0xEFu);
  assert(u8(mem[1]) == 0xBEu);

  // one-word bitfield set => exactly 2 barriers
  reset_barriers();
  vx.set<"bf_one">(0x155u);
  assert(g_barriers == 2);

  // fallback bitfield set => exactly 2 barriers
  reset_barriers();
  vx.set<"bf_cross">(0xA5u);
  assert(g_barriers == 2);

  // nibble sanity (same as reg barrier test)
  assert(u8(mem[3]) == 0x56u);
  assert(u8(mem[4]) == 0x9Au);

  // guard untouched by bitfield sets
  assert(u8(mem[5]) == 0xAAu);

  // guard scalar set => exactly 2 barriers
  reset_barriers();
  vx.set<"guard">(0x5Au);
  assert(g_barriers == 2);
  assert(u8(mem[5]) == 0x5Au);

  // multiple sets => linear accumulation (2 per set); reads still do not add barriers
  reset_barriers();
  vx.set<"s16_le">(0x1111u);
  vx.set<"bf_one">(0x3u);
  vx.set<"bf_cross">(0x7Fu);
  vx.set<"guard">(0x33u);
  assert(g_barriers == 2u * 4u);

  (void)vx.get<"s16_le">();
  (void)vx.get<"bf_one">();
  (void)vx.get<"bf_cross">();
  (void)vx.get<"guard">();
  assert(g_barriers == 2u * 4u);

  return 0;
}
