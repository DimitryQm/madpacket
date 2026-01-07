#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// Tests for bitfield masking behavior:
// - For bitfield integer fields, set(v) stores only low 'bits' bits.
// - For bits==64 in a BITFIELD (non-byte-aligned 64-bit), masking must behave as "all ones"
//   without undefined shifts.
// - Writes preserve neighboring bits (RMW window preserves bits outside field).

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }
static inline constexpr std::uint64_t mask64(std::size_t bits) {
  return (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);
}

int main() {
  using namespace mad;


  // Case A: 13-bit unsigned bitfield with neighbors; verify masking and preservation.
  // Layout: pre(3) | u13 | post(4) => total 20 bits => 3 bytes
  // u13 crosses bytes and is bitfield path (not scalar width).
  using A = packet<
    u3<"pre">,
    ubits<13, "u13">,
    u4<"post">z
  >;

  static_assert(A::total_bits == 20);
  static_assert(A::total_bytes == 3);

  {
    std::array<std::byte, A::total_bytes> buf{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    auto v = make_view<A>(buf.data(), buf.size());

    // Start with all ones; set pre/post to known values to test preservation.
    v.set<"pre">(0b001);
    v.set<"post">(0xAu);

    const auto pre_before  = v.get<"pre">();
    const auto post_before = v.get<"post">();
    assert(pre_before == 1u);
    assert(post_before == 0xAu);

    // Now set u13 with a value that has bits above 13 set.
    const std::uint64_t in = 0xFFFFu; // low 16 bits ones; mask should keep low 13 => 0x1FFF
    v.set<"u13">(in);
    assert(v.get<"u13">() == (in & mask64(13)));

    // Neighbors must remain unchanged.
    assert(v.get<"pre">() == pre_before);
    assert(v.get<"post">() == post_before);

    // Flip u13 again and ensure pre/post still unchanged.
    v.set<"u13">(0x2000u); // bit 13 set -> should truncate to 0
    assert(v.get<"u13">() == 0u);
    assert(v.get<"pre">() == pre_before);
    assert(v.get<"post">() == post_before);
  }

  // Case B: bits==64 but as a BITFIELD (shift != 0).
  // Layout: pad1 | u64bits | tail7 => total 72 bits => 9 bytes
  // Field u64bits starts at bit1, so it must use the bitfield path (not scalar).
  // This specifically stress-tests mask logic for bits==64.
  using B = packet<
    pad_bits<1>,
    ubits<64, "u64bits">,
    u7<"tail7">
  >;

  static_assert(B::total_bits == 1 + 64 + 7);
  static_assert(B::total_bytes == 9);

  {
    std::array<std::byte, B::total_bytes> buf{};
    auto v = make_view<B>(buf.data(), buf.size());

    // Set tail7 to a known value; ensure later writes don't clobber it.
    v.set<"tail7">(0x55u);

    // Write all ones to the 64-bit bitfield.
    v.set<"u64bits">(0xFFFFFFFFFFFFFFFFull);
    assert(v.get<"u64bits">() == 0xFFFFFFFFFFFFFFFFull);
    assert(v.get<"tail7">() == 0x55u);

    // Now write a pattern and verify exact bit placement by checking adjacent bits.
    v.set<"u64bits">(0x0123456789ABCDEFull);
    assert(v.get<"u64bits">() == 0x0123456789ABCDEFull);
    assert(v.get<"tail7">() == 0x55u);

    // Neighbor preservation: set tail7, ensure u64bits unchanged.
    const auto before = v.get<"u64bits">();
    v.set<"tail7">(0x7Fu);
    assert(v.get<"u64bits">() == before);

    // Sanity: ensure we are actually shifted by 1 bit:
    assert((u8(buf[0]) & 0x01u) == 0x00u); // pad bit is at bit0, should stay 0
  }

  // Case C: masking for unsigned bitfield does not touch bits outside the field window.
  // Layout: u8 guard0 | pad_bits<4> | u9 | pad_bits<3> | u8 guard1
  // u9 starts at bit12 and spans bytes; only those bits may change.
  using C = packet<
    u8<"g0">,
    pad_bits<4>,
    ubits<9, "u9">,
    pad_bits<3>,
    u8<"g1">
  >;

  static_assert(C::total_bits == 8 + 4 + 9 + 3 + 8);
  static_assert(C::total_bytes == 4);

  {
    std::array<std::byte, C::total_bytes> buf{std::byte{0xA5}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0x5A}};
    auto v = make_view<C>(buf.data(), buf.size());

    // Set guards explicitly (byte-aligned).
    v.set<"g0">(0xA5u);
    v.set<"g1">(0x5Au);

    // Capture initial state of middle bytes for preservation checks around u9.
    const auto b1_before = u8(buf[1]);
    const auto b2_before = u8(buf[2]);

    // Set u9 with big value; should mask to 9 bits (0x1FF).
    v.set<"u9">(0xFFFFu);
    assert(v.get<"u9">() == 0x1FFu);

    // Guards must remain unchanged.
    assert(v.get<"g0">() == 0xA5u);
    assert(v.get<"g1">() == 0x5Au);

    // Only bits 12..20 are allowed to change.
    assert((u8(buf[1]) & 0x0Fu) == (b1_before & 0x0Fu)); // padding bits 8..11 preserved
    assert((u8(buf[2]) & 0xE0u) == (b2_before & 0xE0u)); // padding bits 21..23 preserved
  }

  return 0;
}
