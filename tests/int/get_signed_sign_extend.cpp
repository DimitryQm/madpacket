#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// Tests for sign extension on signed integer fields (both bitfield and byte-aligned scalars).

int main() {
  using namespace mad;

  // Layout:
  // s5: 5-bit signed at bit0 (bitfield path)
  // pad 3 bits -> align next to byte boundary
  // s8: 8-bit signed at bit8 (byte-aligned scalar)
  // s16: 16-bit signed at bit16 (byte-aligned scalar)
  using P = packet<
    ibits<5, "s5">,
    pad_bits<3>,
    i8<"s8">,
    i16<"s16">
  >;

  static_assert(P::total_bits == 5 + 3 + 8 + 16);
  static_assert(P::total_bytes == 4);

  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_view<P>(buf.data(), buf.size());

  // ---- s5 (5-bit signed) ----
  // Write 16 (0b10000). In 5-bit two's complement that's -16.
  v.set<"s5">(16);
  assert(v.get<"s5">() == static_cast<std::int64_t>(-16));

  // Write -1. In 5-bit two's complement that's 0b11111 -> -1.
  v.set<"s5">(-1);
  assert(v.get<"s5">() == static_cast<std::int64_t>(-1));

  // Write +15 (0b01111) should remain +15.
  v.set<"s5">(15);
  assert(v.get<"s5">() == static_cast<std::int64_t>(15));

  // ---- s8 (8-bit signed scalar) ----
  // Store 0x80; as signed 8-bit that's -128.
  v.set<"s8">(0x80);
  assert(v.get<"s8">() == static_cast<std::int64_t>(-128));

  // Store 0xFF; as signed 8-bit that's -1.
  v.set<"s8">(0xFF);
  assert(v.get<"s8">() == static_cast<std::int64_t>(-1));

  // Store 0x7F; as signed 8-bit that's +127.
  v.set<"s8">(0x7F);
  assert(v.get<"s8">() == static_cast<std::int64_t>(127));

  // ---- s16 (16-bit signed scalar) ----
  // Store 0x8000; as signed 16-bit that's -32768.
  v.set<"s16">(0x8000);
  assert(v.get<"s16">() == static_cast<std::int64_t>(-32768));

  // Store 0xFFFF; as signed 16-bit that's -1.
  v.set<"s16">(0xFFFF);
  assert(v.get<"s16">() == static_cast<std::int64_t>(-1));

  // Store 0x7FFF; as signed 16-bit that's +32767.
  v.set<"s16">(0x7FFF);
  assert(v.get<"s16">() == static_cast<std::int64_t>(32767));

  return 0;
}
