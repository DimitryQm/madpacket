#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// Tests for unsigned get() semantics:
// - returns std::uint64_t
// - low 'bits' bits match stored value
// - high bits are zero (zero extension)
// - does not bleed into neighboring bits outside the field

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }
static inline constexpr std::uint64_t mask64(std::size_t bits) {
  return (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);
}

int main() {
  using namespace mad;

  // Case A: small bitfield (12 bits), non-byte-aligned neighbor bits present
  // Layout: pad1 | u12 | pad3 | u8
  // u12 starts at bit1 and spans bytes; u8 is byte-aligned after explicit pad.
 using A = packet<
    pad_bits<1>,
    ubits<12, "u12">,
    pad_bits<3>,
    u8<"tail">
  >;

  static_assert(A::total_bits == 1 + 12 + 3 + 8);
  static_assert(A::total_bytes == 3);

  {
    std::array<std::byte, A::total_bytes> buf{};
    // Fill with all ones so neighboring bits outside u12 are 1.
    buf[0] = std::byte{0xFF};
    buf[1] = std::byte{0xFF};
    buf[2] = std::byte{0xFF};

    auto v = make_view<A>(buf.data(), buf.size());

    // Ensure get() returns uint64_t for unsigned fields
    static_assert(std::is_same_v<decltype(v.get<"u12">()), std::uint64_t>);
    static_assert(std::is_same_v<decltype(v.get<"tail">()), std::uint64_t>);

    const std::uint64_t x = v.get<"u12">();
    // u12 should read 12 bits => max 0xFFF, regardless of neighbors.
    assert((x & ~mask64(12)) == 0ull);
    assert(x == 0xFFFu);

    // Now clear buffer and explicitly set u12 to a value with high bits set in input.
    buf = {std::byte{0}, std::byte{0}, std::byte{0}};
    v.set<"u12">(0xF00Du); // should truncate to low 12 bits => 0x00D
    const std::uint64_t y = v.get<"u12">();
    assert(y == 0x00Du);
    assert((y & ~mask64(12)) == 0ull);

    // tail is u8 at the last byte (byte2)
    v.set<"tail">(0xABu);
    assert(v.get<"tail">() == 0xABu);
    assert(u8(buf[2]) == 0xABu);
  }

  // Case B: byte-aligned scalar unsigned fields still return uint64_t and zero extend
  using B = packet<
    u8<"u8">,
    le_u16<"le16">,
    be_u32<"be32">
  >;

  static_assert(B::total_bytes == 1 + 2 + 4);

  {
    std::array<std::byte, B::total_bytes> buf{};
    auto v = make_view<B>(buf.data(), buf.size());

    v.set<"u8">(0xFFu);
    v.set<"le16">(0xBEEFu);
    v.set<"be32">(0x11223344u);

    // Types
    static_assert(std::is_same_v<decltype(v.get<"u8">()), std::uint64_t>);
    static_assert(std::is_same_v<decltype(v.get<"le16">()), std::uint64_t>);
    static_assert(std::is_same_v<decltype(v.get<"be32">()), std::uint64_t>);

    const auto a = v.get<"u8">();
    const auto b = v.get<"le16">();
    const auto c = v.get<"be32">();

    assert((a & ~mask64(8)) == 0ull);
    assert((b & ~mask64(16)) == 0ull);
    assert((c & ~mask64(32)) == 0ull);

    assert(a == 0xFFu);
    assert(b == 0xBEEFu);
    assert(c == 0x11223344u);

    // Verify be32 byte layout explicitly (host independent)
    // u8 at byte0, le16 at bytes1-2, be32 at bytes3-6.
    assert(u8(buf[3]) == 0x11u);
    assert(u8(buf[4]) == 0x22u);
    assert(u8(buf[5]) == 0x33u);
    assert(u8(buf[6]) == 0x44u);
  }

  // Case C: large bitfield (63 bits) zero-extends in uint64_t
  // (We avoid 64 here; 64-bit bitfield has its own dedicated mask edge-case test elsewhere.)
  using C = packet<
    pad_bits<3>,
    ubits<63, "u63">
  >;

  static_assert(C::total_bits == 66);
  static_assert(C::total_bytes == 9);

  {
    std::array<std::byte, C::total_bytes> buf{};
    auto v = make_view<C>(buf.data(), buf.size());

    // Set to all-ones input; should store low 63 ones.
    v.set<"u63">(~0ull);
    const auto x = v.get<"u63">();
    assert((x >> 63) == 0ull); // top bit must be zero (63-bit field)
    assert(x == ((1ull << 63) - 1ull));

    // Ensure setting a smaller value doesn't pick up padding bits.
    v.set<"u63">(0x123456789ABCDEF0ull);
    const auto y = v.get<"u63">();
    assert((y >> 63) == 0ull);
    assert((y & ((1ull << 63) - 1ull)) == (0x123456789ABCDEF0ull & ((1ull << 63) - 1ull)));
  }

  return 0;
}
