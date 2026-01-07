#include <array>
#include <cassert>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/endian/native_is_host_dependent.cpp
//
// Contract (§6):
//  - native endian fields store bytes in *host* endianness.
//  - little_endian and big_endian tags store bytes in those orders independent of host.
//
// We validate:
//  1) native u32 byte pattern matches host order.
//  2) native equals little on little-endian hosts; native equals big on big-endian hosts.
//  3) le/be are stable and always produce their respective byte order.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  using P = packet<
    u32<"n32">,      // native endian
    le_u32<"le32">,  // little endian
    be_u32<"be32">   // big endian
  >;

  static_assert(P::total_bytes == 12);

  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_view<P>(buf.data(), buf.size());

  constexpr std::uint32_t value = 0x11223344u;

  v.set<"n32">(value);
  v.set<"le32">(value);
  v.set<"be32">(value);

  // Verify roundtrip values
  assert(v.get<"n32">() == value);
  assert(v.get<"le32">() == value);
  assert(v.get<"be32">() == value);

  // le32 must be 44 33 22 11 regardless of host.
  assert(u8(buf[4]) == 0x44u);
  assert(u8(buf[5]) == 0x33u);
  assert(u8(buf[6]) == 0x22u);
  assert(u8(buf[7]) == 0x11u);

  // be32 must be 11 22 33 44 regardless of host.
  assert(u8(buf[8])  == 0x11u);
  assert(u8(buf[9])  == 0x22u);
  assert(u8(buf[10]) == 0x33u);
  assert(u8(buf[11]) == 0x44u);

  // native (buf[0..3]) is host dependent:
  if constexpr (std::endian::native == std::endian::little) {
    // native must equal little-endian order for this value
    assert(u8(buf[0]) == 0x44u);
    assert(u8(buf[1]) == 0x33u);
    assert(u8(buf[2]) == 0x22u);
    assert(u8(buf[3]) == 0x11u);

    // And native bytes must match le bytes exactly
    for (int i = 0; i < 4; ++i) assert(buf[i] == buf[4 + i]);
  } else if constexpr (std::endian::native == std::endian::big) {
    // native must equal big-endian order for this value
    assert(u8(buf[0]) == 0x11u);
    assert(u8(buf[1]) == 0x22u);
    assert(u8(buf[2]) == 0x33u);
    assert(u8(buf[3]) == 0x44u);

    // And native bytes must match be bytes exactly
    for (int i = 0; i < 4; ++i) assert(buf[i] == buf[8 + i]);
  } else {
    // Uncommon mixed-endian machines; we only require that "native" differs
    // from at least one of the explicit endianness encodings for this test vector.
    bool same_as_le = true, same_as_be = true;
    for (int i = 0; i < 4; ++i) {
      same_as_le &= (buf[i] == buf[4 + i]);
      same_as_be &= (buf[i] == buf[8 + i]);
    }
    assert(!(same_as_le && same_as_be));
  }

  // Additional sanity: setting different values updates accordingly.
  v.set<"n32">(0xA1B2C3D4u);
  const auto n = v.get<"n32">();
  assert(static_cast<std::uint32_t>(n) == 0xA1B2C3D4u);


  // Bonus: native vs explicit endianness for 16-bit, same rules
  // This gives additional coverage for scalar widths other than 32.
  using Q = packet<
    u16<"n16">,
    le_u16<"le16">,
    be_u16<"be16">,
    u8<"guard">
  >;
  static_assert(Q::total_bytes == 2 + 2 + 2 + 1);

  std::array<std::byte, Q::total_bytes> buf2{};
  auto q = make_view<Q>(buf2.data(), buf2.size());

  constexpr std::uint16_t v16 = 0xCAFEu;
  q.set<"n16">(v16);
  q.set<"le16">(v16);
  q.set<"be16">(v16);
  q.set<"guard">(0x5Au);

  assert(q.get<"n16">() == v16);
  assert(q.get<"le16">() == v16);
  assert(q.get<"be16">() == v16);
  assert(q.get<"guard">() == 0x5Au);

  // Explicit encodings:
  // le16 must be FE CA and be16 must be CA FE, regardless of host.
  assert(u8(buf2[2]) == 0xFEu);
  assert(u8(buf2[3]) == 0xCAu);
  assert(u8(buf2[4]) == 0xCAu);
  assert(u8(buf2[5]) == 0xFEu);

  if constexpr (std::endian::native == std::endian::little) {
    assert(u8(buf2[0]) == 0xFEu);
    assert(u8(buf2[1]) == 0xCAu);
  } else if constexpr (std::endian::native == std::endian::big) {
    assert(u8(buf2[0]) == 0xCAu);
    assert(u8(buf2[1]) == 0xFEu);
  }

  return 0;
}
