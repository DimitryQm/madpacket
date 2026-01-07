#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// Tests that set<Name>(v) truncates/masks modulo 2^bits for both scalar and bitfield ints.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  using P = packet<
    u8<"u8">,          // scalar, 8 bits
    u16<"u16">,        // scalar, 16 bits (native endian)
    ubits<13, "u13">,  // bitfield, 13 bits (byte-aligned but non-scalar-width)
    ibits<5, "i5">,    // signed bitfield, 5 bits
    pad_bits<3>,       // align next
    i16<"i16">         // signed scalar, 16 bits
  >;

  // total bits: 8 + 16 + 13 + 5 + 3 + 16 = 61 => 8 bytes (ceil)
  static_assert(P::total_bits == 61);
  static_assert(P::total_bytes == 8);

  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_view<P>(buf.data(), buf.size());

  // ---- u8 truncation is visible in raw memory (single byte) ----
  v.set<"u8">(0x1FFu);
  assert(u8(buf[0]) == 0xFFu);
  assert(v.get<"u8">() == 0xFFu);

  v.set<"u8">(0x100u);
  assert(u8(buf[0]) == 0x00u);
  assert(v.get<"u8">() == 0x00u);

  // ---- u16 truncation (native endian in memory; validate via get) ----
  v.set<"u16">(0x1'2345u); // truncates to 0x2345
  assert(v.get<"u16">() == 0x2345u);

  v.set<"u16">(0x1'0000u); // truncates to 0x0000
  assert(v.get<"u16">() == 0x0000u);

  // ---- u13 bitfield truncation (mask to 13 bits) ----
  v.set<"u13">(0x3FFFu); // 14 bits of 1 -> trunc to 0x1FFF
  assert(v.get<"u13">() == 0x1FFFu);

  v.set<"u13">(0x2000u); // 1<<13 -> trunc to 0
  assert(v.get<"u13">() == 0x0000u);

  // ---- signed 5-bit behavior: modulo 32, then sign-extend on get ----
  v.set<"i5">(-1);
  assert(v.get<"i5">() == static_cast<std::int64_t>(-1));

  // Writing +31 stores 0b11111 which is -1 when interpreted as 5-bit signed.
  v.set<"i5">(31);
  assert(v.get<"i5">() == static_cast<std::int64_t>(-1));

  // -33 modulo 32 indicates low 5 bits are 1 -> stores +1
  v.set<"i5">(-33);
  assert(v.get<"i5">() == static_cast<std::int64_t>(1));

  // ---- signed 16-bit scalar truncation: 0x1FFFF -> 0xFFFF -> -1 ----
  v.set<"i16">(0x1FFFFu);
  assert(v.get<"i16">() == static_cast<std::int64_t>(-1));

  return 0;
}
