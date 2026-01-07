#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// Tests for signed set() semantics with negative values:
// - Negative values are stored as two's complement, truncated to the field width.
// - This applies to both scalar signed fields and signed bitfields.
// - For scalar signed fields, we use explicit endian tags via int_field to assert exact bytes
//   in a host-independent way.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  // Signed scalars with explicit LE/BE storage to avoid host-native ambiguity.
  using P = packet<
    int_field<"s16le", 16, true, little_endian_t>,
    int_field<"s16be", 16, true, big_endian_t>,
    int_field<"s32le", 32, true, little_endian_t>,
    int_field<"s32be", 32, true, big_endian_t>,
    ibits<5, "s5">,            // signed bitfield for truncation behavior
    pad_bits<3>,
    u8<"guard">                // guard byte after bitfield to catch overruns
  >;

  // bytes: 2 + 2 + 4 + 4 = 12, then 5+3+8=16 bits => +2 bytes => total 14
  static_assert(P::total_bytes == 14);

  {
    std::array<std::byte, P::total_bytes> buf{};
    auto v = make_view<P>(buf.data(), buf.size());

    // Initialize guard to a known pattern to detect accidental clobber.
    v.set<"guard">(0xA5u);

    // ---- s16le negative values ----
    v.set<"s16le">(-1);
    assert(u8(buf[0]) == 0xFFu);
    assert(u8(buf[1]) == 0xFFu);
    assert(v.get<"s16le">() == static_cast<std::int64_t>(-1));

    v.set<"s16le">(-2);
    assert(u8(buf[0]) == 0xFEu);
    assert(u8(buf[1]) == 0xFFu);
    assert(v.get<"s16le">() == static_cast<std::int64_t>(-2));

    // ---- s16be negative values ----
    v.set<"s16be">(-1);
    assert(u8(buf[2]) == 0xFFu);
    assert(u8(buf[3]) == 0xFFu);
    assert(v.get<"s16be">() == static_cast<std::int64_t>(-1));

    v.set<"s16be">(-2);
    assert(u8(buf[2]) == 0xFFu);
    assert(u8(buf[3]) == 0xFEu);
    assert(v.get<"s16be">() == static_cast<std::int64_t>(-2));

    // ---- s32le / s32be with a more distinctive negative pattern ----
    v.set<"s32le">(static_cast<std::int32_t>(-0x123456)); // two's complement: 0xFFEDCBAA
    // expected LE bytes: AA CB ED FF
    assert(u8(buf[4]) == 0xAAu);
    assert(u8(buf[5]) == 0xCBu);
    assert(u8(buf[6]) == 0xEDu);
    assert(u8(buf[7]) == 0xFFu);

    v.set<"s32be">(static_cast<std::int32_t>(-0x123456));
    // expected BE bytes: FF ED CB AA
    assert(u8(buf[8])  == 0xFFu);
    assert(u8(buf[9])  == 0xEDu);
    assert(u8(buf[10]) == 0xCBu);
    assert(u8(buf[11]) == 0xAAu);

    // ---- s5 bitfield truncation for negative values ----
    // s5 starts at bit offset 96 (12 bytes * 8) => byte12 bit0.
    // Setting -1 should store 0b11111 into low 5 bits of byte12.
    v.set<"s5">(-1);
    assert((u8(buf[12]) & 0x1Fu) == 0x1Fu);
    assert(v.get<"s5">() == static_cast<std::int64_t>(-1));
    assert(v.get<"guard">() == 0xA5u);

    // Setting -33: low 5 bits are 31 => -1 when interpreted as 5-bit signed.
    v.set<"s5">(-33);
    assert((u8(buf[12]) & 0x1Fu) == 0x1Fu);
    assert(v.get<"s5">() == static_cast<std::int64_t>(-1));
    assert(v.get<"guard">() == 0xA5u);

    // Setting -32 truncates to 0 => 0b00000 => 0
    v.set<"s5">(-32);
    assert((u8(buf[12]) & 0x1Fu) == 0x00u);
    assert(v.get<"s5">() == static_cast<std::int64_t>(0));
    assert(v.get<"guard">() == 0xA5u);
  }

  return 0;
}
