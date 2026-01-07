#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// Golden tests for byte-aligned scalar endian tags (le/be) on 16/32/64-bit fields.
// These tests are host-endianness independent because they assert the exact byte layout
// for explicit le/be tags.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  using P = packet<
    le_u16<"le16">,
    be_u16<"be16">,
    le_u32<"le32">,
    be_u32<"be32">,
    le_u64<"le64">,
    be_u64<"be64">
  >;

  static_assert(P::total_bytes == 2 + 2 + 4 + 4 + 8 + 8);

  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_view<P>(buf.data(), buf.size());

  v.set<"le16">(0x1234u);
  v.set<"be16">(0x1234u);
  v.set<"le32">(0x11223344u);
  v.set<"be32">(0x11223344u);
  v.set<"le64">(0x0102030405060708ull);
  v.set<"be64">(0x0102030405060708ull);

  // Expected byte layout:
  // le16: 34 12
  // be16: 12 34
  // le32: 44 33 22 11
  // be32: 11 22 33 44
  // le64: 08 07 06 05 04 03 02 01
  // be64: 01 02 03 04 05 06 07 08

  const unsigned expect[] = {
    0x34, 0x12,
    0x12, 0x34,
    0x44, 0x33, 0x22, 0x11,
    0x11, 0x22, 0x33, 0x44,
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
  };

  for (std::size_t i = 0; i < P::total_bytes; ++i) {
    assert(u8(buf[i]) == expect[i]);
  }

  // Roundtrip numeric values
  assert(v.get<"le16">() == 0x1234u);
  assert(v.get<"be16">() == 0x1234u);
  assert(v.get<"le32">() == 0x11223344u);
  assert(v.get<"be32">() == 0x11223344u);
  assert(v.get<"le64">() == 0x0102030405060708ull);
  assert(v.get<"be64">() == 0x0102030405060708ull);

  // Also validate decoding from raw pre-filled buffers.
  std::array<std::byte, P::total_bytes> buf2{};
  for (std::size_t i = 0; i < P::total_bytes; ++i) buf2[i] = std::byte(expect[i]);
  auto v2 = make_view<P>(buf2.data(), buf2.size());

  assert(v2.get<"le16">() == 0x1234u);
  assert(v2.get<"be16">() == 0x1234u);
  assert(v2.get<"le32">() == 0x11223344u);
  assert(v2.get<"be32">() == 0x11223344u);
  assert(v2.get<"le64">() == 0x0102030405060708ull);
  assert(v2.get<"be64">() == 0x0102030405060708ull);

  return 0;
}
