#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp
//
// Layout Model (§7) + Semantics Contract (§8):
//   bytes_field<Name,N> and subpacket_field<Packet,Name> are addressed by
//   byte_offset = (bit_offset >> 3) only; intra-byte shifts are ignored.
//   Therefore they MUST begin on a byte boundary (bit_offset % 8 == 0).
//
// NOTE :
//   mad::packet validates this at type formation time via static_assert in
//   Packet::validate(). So misaligned bytes/subpacket layouts are compile-time ill-formed.
//   This file therefore focuses on POSITIVE “golden” behavior for correctly-aligned layouts,
//   including pointer math and byte mapping for both bytes views and nested views.
//

static inline unsigned byte_u(std::byte b) { return std::to_integer<unsigned>(b); }
static inline std::byte b8(unsigned v) { return std::byte{static_cast<unsigned char>(v & 0xFFu)}; }

int main() {
  using namespace mad;

  // Case A: bytes field properly byte-aligned after bitfields + explicit pad
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
    buf[0] = b8(0xAB);
    buf[1] = b8(0x11);
    buf[2] = b8(0x22);
    buf[3] = b8(0x33);
    buf[4] = b8(0xCD);

    auto v = make_view<A>(buf.data(), buf.size());

    v.set<"pre4">(0x5u);
    v.set<"tail">(0xEEu);

    // bytes view points exactly at byte1.
    auto payload = v.get<"payload">();
    assert(payload.size() == 3);
    assert(payload.data() == (buf.data() + 1));

    auto sp = payload.as_span();
    assert(sp.size() == 3);
    assert(sp.data() == (buf.data() + 1));

    // Mutate through span and verify correct region is modified.
    sp[0] = b8(0xDE);
    sp[1] = b8(0xAD);
    sp[2] = b8(0xBE);

    assert(byte_u(buf[1]) == 0xDEu);
    assert(byte_u(buf[2]) == 0xADu);
    assert(byte_u(buf[3]) == 0xBEu);

    // Ensure pre/tail still correct and not clobbered.
    assert(v.get<"pre4">() == 0x5u);
    assert(v.get<"tail">() == 0xEEu);
  }

  // Case B: subpacket field properly byte-aligned; nested view pointer + mapping
  // Sub packet uses a mix of scalar + bitfields but is whole-byte sized (24 bits).
  using Sub = packet<
    u8<"sx">,           // byte0
    ubits<3, "a">,      // byte1 bits0..2
    ubits<5, "b">,      // byte1 bits3..7
    u8<"sy">            // byte2
  >;
  static_assert(Sub::total_bits == 24);
  static_assert(Sub::total_bits % 8 == 0);
  static_assert(Sub::total_bytes == 3);

  // Parent aligns sub to byte boundary and surrounds it with guards.
  using B = packet<
    u1<"flag">,
    pad_bits<7>,              // align to byte1
    subpacket<Sub, "sub">,    // starts at byte1
    u8<"tail">                // byte4
  >;
  static_assert(B::total_bytes == 1 + 3 + 1);

  {
    std::array<std::byte, B::total_bytes> buf{};
    // seed with visible pattern
    buf[0] = b8(0x00);
    buf[1] = b8(0x11);
    buf[2] = b8(0x22);
    buf[3] = b8(0x33);
    buf[4] = b8(0x44);

    auto v = make_view<B>(buf.data(), buf.size());

    v.set<"flag">(1u);
    v.set<"tail">(0xAAu);

    auto s = v.get<"sub">();
    // Critical invariant: nested view base pointer is parent base + byte_offset
    assert(s.data() == (buf.data() + 1));
    assert(s.size_bytes() == Sub::total_bytes);

    // Set nested fields and validate exact byte mapping:
    s.set<"sx">(0x12u);
    s.set<"a">(0b101u);     // 5
    s.set<"b">(0b11001u);   // 25
    s.set<"sy">(0x34u);

    // Sub layout bytes:
    //   byte1 = sx
    //   byte2 = (b<<3)|a
    //   byte3 = sy
    assert(byte_u(buf[1]) == 0x12u);
    assert(byte_u(buf[2]) == (((25u & 0x1Fu) << 3) | (5u & 0x7u)));
    assert(byte_u(buf[3]) == 0x34u);

    // Parent tail remains at byte4.
    assert(byte_u(buf[4]) == 0xAAu);
    assert(v.get<"tail">() == 0xAAu);

    // Readback agrees.
    assert(static_cast<unsigned>(s.get<"sx">()) == 0x12u);
    assert(static_cast<unsigned>(s.get<"a">()) == 5u);
    assert(static_cast<unsigned>(s.get<"b">()) == 25u);
    assert(static_cast<unsigned>(s.get<"sy">()) == 0x34u);

    // Sweep a/b a bit to catch intra-byte offset mistakes.
    for (unsigned aa = 0; aa < 8; ++aa) {
      for (unsigned bb = 0; bb < 32; bb += 7) {
        s.set<"a">(aa);
        s.set<"b">(bb);
        const unsigned expect = ((bb & 0x1Fu) << 3) | (aa & 0x7u);
        assert(byte_u(buf[2]) == expect);
        assert(static_cast<unsigned>(s.get<"a">()) == (aa & 0x7u));
        assert(static_cast<unsigned>(s.get<"b">()) == (bb & 0x1Fu));
      }
    }
  }

  // Case C: bytes field at offset 0 (trivial alignment) still returns exact slice
  using C = packet<
    bytes<"hdr", 2>,
    le_u16<"x">,
    u8<"tail">
  >;
  static_assert(C::total_bytes == 2 + 2 + 1);

  {
    std::array<std::byte, C::total_bytes> buf{};
    buf[0] = b8(0xA0);
    buf[1] = b8(0xB1);
    buf[2] = b8(0x00);
    buf[3] = b8(0x00);
    buf[4] = b8(0x00);

    auto v = make_view<C>(buf.data(), buf.size());

    auto hdr = v.get<"hdr">();
    assert(hdr.data() == buf.data());
    assert(hdr.size() == 2);

    // Change header bytes through span.
    auto sp = hdr.as_span();
    sp[0] = b8(0xDE);
    sp[1] = b8(0xAD);
    assert(byte_u(buf[0]) == 0xDEu);
    assert(byte_u(buf[1]) == 0xADu);

    // Scalar access starts at byte2.
    v.set<"x">(0xBEEFu);
    assert(byte_u(buf[2]) == 0xEFu);
    assert(byte_u(buf[3]) == 0xBEu);

    v.set<"tail">(0x5Au);
    assert(byte_u(buf[4]) == 0x5Au);
  }
  // Negative cases are now compile-time errors (Packet::validate()).
  // Keep them as compile-fail tests, not in this runtime file.
#if 0
  using BadBytes = packet<u4<"x">, bytes<"bad", 1>>;
  using Sub2 = packet<u8<"a">>;
  using BadSubStart = packet<u1<"f">, subpacket<Sub2, "s">>;
  using SubWeird = packet<u8<"a">, u4<"b">>; // 12 bits
  using BadSubSize = packet<subpacket<SubWeird, "s">>;
#endif

  return 0;
}
