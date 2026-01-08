#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp
//
// Contract (§8):
//   bytes_field<Name,N> and subpacket_field<Packet,Name> are addressed by
//   byte = (bit_offset >> 3) only; intra-byte shifts are ignored.
//   Therefore they must begin on a byte boundary (bit_offset % 8 == 0).
//
// This test does two things:
//   (1) Validates correct behavior for byte-aligned bytes/subpacket fields.
//   (2) Demonstrates the *current* observable behavior for misaligned usage
//       (pointer floors to the previous byte), so the contract can treat it
//       as undefined behavior at the library level.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  // Case A: bytes field properly byte-aligned
  // Layout:
  //   pre4 + pad4 => payload starts at byte1
  using A = packet<
    u4<"pre4">,
    pad_bits<4>,
    bytes<"payload", 3>,
    u8<"tail">
  >;
  static_assert(A::total_bytes == 1 + 3 + 1);

  {
    std::array<std::byte, A::total_bytes> buf{};
    buf[0] = std::byte{0xAB};
    buf[1] = std::byte{0x11};
    buf[2] = std::byte{0x22};
    buf[3] = std::byte{0x33};
    buf[4] = std::byte{0xCD};

    auto v = make_view<A>(buf.data(), buf.size());

    v.set<"pre4">(0x5u);
    v.set<"tail">(0xEEu);

    auto payload = v.get<"payload">();
    assert(payload.size() == 3);
    assert(payload.data() == (buf.data() + 1));

    auto sp = payload.as_span();
    assert(sp.size() == 3);
    assert(sp.data() == (buf.data() + 1));

    // Write through the bytes view and confirm correct region is modified.
    sp[0] = std::byte{0xDE};
    sp[1] = std::byte{0xAD};
    sp[2] = std::byte{0xBE};

    assert(u8(buf[1]) == 0xDEu);
    assert(u8(buf[2]) == 0xADu);
    assert(u8(buf[3]) == 0xBEu);

    // Ensure pre/tail still correct and not clobbered.
    assert(v.get<"pre4">() == 0x5u);
    assert(v.get<"tail">() == 0xEEu);
  }

  // Case B: subpacket field properly byte-aligned
  using Sub = packet<
    u8<"sx">,
    u8<"sy">
  >;

  using B = packet<
    u1<"flag">,
    pad_bits<7>,            // align to byte
    subpacket<Sub, "sub">,  // starts at byte1
    u8<"tail">
  >;

  static_assert(Sub::total_bytes == 2);
  static_assert(B::total_bytes == 1 + 2 + 1);

  {
    std::array<std::byte, B::total_bytes> buf{};
    auto v = make_view<B>(buf.data(), buf.size());

    v.set<"flag">(1);
    v.set<"tail">(0xAAu);

    auto s = v.get<"sub">();
    s.set<"sx">(0x12u);
    s.set<"sy">(0x34u);

    // sub is byte-aligned at byte1.
    assert(u8(buf[1]) == 0x12u);
    assert(u8(buf[2]) == 0x34u);

    // tail at byte3
    assert(u8(buf[3]) == 0xAAu);
    assert(v.get<"tail">() == 0xAAu);
  }

  // Case C: bytes field misaligned (demonstration of why it's UB at library level)
  //
  // Layout: pre4 then payload bytes (starts at bit offset 4, NOT byte-aligned).
  // In current implementation, get<"payload"> uses (bit_off >> 3) and returns base+0,
  // meaning payload.data() points at buf[0], *not* at a nibble boundary.
  using C = packet<
    u4<"pre4">,
    bytes<"payload", 2>, // MISALIGNED: bit offset is 4
    u8<"tail">
  >;
  static_assert(C::total_bytes == 1 + 2 + 1); // still computed as ceil(total_bits/8)

  {
    std::array<std::byte, C::total_bytes> buf{};
    buf[0] = std::byte{0xF0};
    buf[1] = std::byte{0x11};
    buf[2] = std::byte{0x22};
    buf[3] = std::byte{0x33};

    auto v = make_view<C>(buf.data(), buf.size());
    v.set<"pre4">(0xAu); // only low nibble should be A

    auto payload = v.get<"payload">();
    // Demonstrate the current pointer flooring behavior.
    assert(payload.data() == buf.data() + 0);
    assert(payload.size() == 2);

    // Mutating "payload" will clobber byte0 (which also contains pre4 bits).
    auto sp = payload.as_span();
    sp[0] = std::byte{0xDE};
    sp[1] = std::byte{0xAD};

    assert(u8(buf[0]) == 0xDEu);
    assert(u8(buf[1]) == 0xADu);

    // tail is still a byte-aligned u8 at the end; ensure it's not affected by payload writes.
    v.set<"tail">(0x5Au);
    assert(v.get<"tail">() == 0x5Au);
    assert(u8(buf[3]) == 0x5Au);
  }

  // Case D: subpacket misaligned (demonstration)
  //
  // Layout: pre4 then subpacket (starts at bit offset 4).
  // Current implementation returns base + (bit_off >> 3) == base+0 for subpacket view.
  // Nested writes will clobber the first bytes, including the nibble that contains pre4.
  using D = packet<
    u4<"pre4">,
    subpacket<Sub, "sub">, // MISALIGNED
    u8<"tail">
  >;

  {
    std::array<std::byte, D::total_bytes> buf{};
    buf[0] = std::byte{0x0F};
    buf[1] = std::byte{0x00};
    buf[2] = std::byte{0x00};
    buf[3] = std::byte{0x00};

    auto v = make_view<D>(buf.data(), buf.size());
    v.set<"pre4">(0x3u);

    auto s = v.get<"sub">();
    // Implementation floors to byte0.
    s.set<"sx">(0x99u);
    s.set<"sy">(0x88u);

    assert(u8(buf[0]) == 0x99u);
    assert(u8(buf[1]) == 0x88u);

    // tail should still work.
    v.set<"tail">(0x77u);
    assert(u8(buf[3]) == 0x77u);
  }

  return 0;
}
