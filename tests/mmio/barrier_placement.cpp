#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

// tests/mmio/barrier_placement.cpp
//
// Contract (§10, §12):
//   - For MMIO *stores* (set), MAD_MMIO_BARRIER() is called immediately
//     before and immediately after the store sequence.
//   - There is no barrier automatically inserted around get.
//   - For bitfield sets, the barrier wraps either the bus-word store (one-word RMW path)
//     or the byte-window store sequence (fallback path).
//
// This test instruments MAD_MMIO_BARRIER to count invocations and verifies that:
//   - get does not invoke the barrier
//   - set invokes it exactly twice per operation (before + after), for:
//       * scalar stores
//       * one-word bitfield RMW stores
//       * fallback byte-window bitfield stores
//
// NOTE: This does NOT prove the barrier is the correct fence for your target.
// It only validates that the library calls the macro in the promised places.
//
// IMPORTANT: Define MAD_MMIO_BARRIER BEFORE including the header so the library
// uses our instrumentation.

static inline std::uint64_t g_barriers = 0;

#define MAD_MMIO_BARRIER() do { ++g_barriers; } while(0)
#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline void store_bytes(std::byte* p, std::initializer_list<unsigned> bytes) {
  std::size_t i = 0;
  for (unsigned b : bytes) p[i++] = std::byte{static_cast<unsigned char>(b)};
}

static inline void reset_barriers() { g_barriers = 0; }

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::bits == 32);
  static_assert(Bus::align == 4);

  // Packet layout engineered to cover multiple store paths:
  //
  // - s16: scalar field at byte offset 0 (exact-width scalar store path)
  // - bf_one: bitfield fully contained in word0 => one-word bus RMW path
  // - bf_cross: bitfield crossing word boundary (bit 28..35) => fallback byte-window path
  // - guard: a byte beyond the bitfields, for additional non-touch sanity
  //
  // Bit offsets:
  //   s16      : [0..16)
  //   bf_one   : [16..26) (fits in bus word0 bits 0..31)
  //   pad2     : [26..28)
  //   bf_cross : [28..36) (crosses from word0 to word1) => fallback
  //   pad4     : [36..40)
  //   guard    : [40..48)
  using P = mad::packet<
    mad::le_u16<"s16">,
    mad::ubits<10, "bf_one">,
    mad::pad_bits<2>,
    mad::ubits<8, "bf_cross">,
    mad::pad_bits<4>,
    mad::u8<"guard">
  >;

  static_assert(P::total_bytes == 6);

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  store_bytes(mem.data(), {0x10, 0x32, 0x54, 0x76, 0x98, 0xAA});

  volatile std::byte* vbase = reinterpret_cast<volatile std::byte*>(mem.data());
  auto v = mad::reg::make_view<P, Bus>(reinterpret_cast<volatile void*>(vbase));

  // Barrier must NOT be called by get (reads).
  reset_barriers();
  (void)v.get<"s16">();
  (void)v.get<"bf_one">();
  (void)v.get<"bf_cross">();
  (void)v.get<"guard">();
  assert(g_barriers == 0);

  // Scalar set => exactly 2 barriers.
  reset_barriers();
  v.set<"s16">(0xBEEFu);
  assert(g_barriers == 2);

  // Validate the bytes changed as LE (just additional sanity that we really exercised set).
  assert(u8(mem[0]) == 0xEFu);
  assert(u8(mem[1]) == 0xBEu);

  // One-word bitfield set => exactly 2 barriers (bus-word RMW path).
  reset_barriers();
  v.set<"bf_one">(0x155u);
  assert(g_barriers == 2);

  // Fallback bitfield set (crosses word boundary) => exactly 2 barriers.
  reset_barriers();
  v.set<"bf_cross">(0xA5u);
  assert(g_barriers == 2);

  // The fallback field spans byte3 high nibble and byte4 low nibble; validate it changed.
  // Original byte3=0x76, byte4=0x98. After set(0xA5):
  //   byte3 high nibble becomes 0x5, low nibble preserved (0x6) => 0x56
  //   byte4 low nibble becomes 0xA, high nibble preserved (0x9) => 0x9A
  assert(u8(mem[3]) == 0x56u);
  assert(u8(mem[4]) == 0x9Au);

  // Guard byte should not be modified by the bitfield set(s).
  assert(u8(mem[5]) == 0xAAu);

  // Another scalar set (guard) => exactly 2 barriers.
  reset_barriers();
  v.set<"guard">(0x5Au);
  assert(g_barriers == 2);
  assert(u8(mem[5]) == 0x5Au);

  // Multiple sets without resetting must accumulate linearly: 2 barriers per set.
  reset_barriers();
  v.set<"s16">(0x1111u);
  v.set<"bf_one">(0x3u);
  v.set<"bf_cross">(0x7Fu);
  v.set<"guard">(0x33u);
  assert(g_barriers == 2u * 4u);

  // And reads still do not add barriers.
  (void)v.get<"s16">();
  (void)v.get<"bf_one">();
  (void)v.get<"bf_cross">();
  (void)v.get<"guard">();
  assert(g_barriers == 2u * 4u);

  return 0;
}
