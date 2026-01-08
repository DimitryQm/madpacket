#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <bit>

#include "madpacket.hpp"

// tests/mmio/basic_scalar_endian.cpp
//
// Contract (§10, scalar fields):
//   - reg::view provides get/set for byte-aligned scalar integer fields with explicit endian tags
//     (little/big/native), identical in meaning to non-volatile views.
//   - When possible, scalar MMIO reads/writes may use typed volatile accesses; if not possible
//     they fall back to bytewise volatile operations. Regardless of mechanism, the stored byte
//     order MUST match the declared endian tag.
//
// This test validates the *observable* semantics:
//   - After set, bytes in memory match the tag (LE vs BE) exactly.
//   - get returns the numeric value you set.
//   - native endian fields store bytes according to std::endian::native (host dependent).
//
// We use a normal aligned byte array and bind it as volatile MMIO via reg::make_view.
// This is still "fake MMIO", but it exercises the volatile code paths in the library.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

static inline void expect_bytes(std::byte const* p, std::initializer_list<unsigned> bytes) {
  std::size_t i = 0;
  for (unsigned b : bytes) {
    assert(u8(p[i]) == b);
    ++i;
  }
}

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::align == 4);

  using P = mad::packet<
    mad::u8<"a">,
    mad::le_u16<"le16">,
    mad::be_u16<"be16">,
    mad::u32<"n32">,     // native endian (default tag)
    mad::le_u32<"le32">,
    mad::be_u32<"be32">,
    mad::u8<"guard">
  >;

  static_assert(P::total_bytes == 1 + 2 + 2 + 4 + 4 + 4 + 1);

  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  for (auto& b : mem) b = std::byte{0xCC}; // poison pattern

  volatile std::byte* vbase = reinterpret_cast<volatile std::byte*>(mem.data());
  auto v = mad::reg::make_view<P, Bus>(reinterpret_cast<volatile void*>(vbase));

  // Set values.
  v.set<"a">(0x11u);
  v.set<"le16">(0xBEEFu);
  v.set<"be16">(0xCAFEu);
  v.set<"n32">(0x11223344u);
  v.set<"le32">(0xA1B2C3D4u);
  v.set<"be32">(0x01020304u);
  v.set<"guard">(0x5Au);

  // Readback must match.
  assert(v.get<"a">() == 0x11u);
  assert(v.get<"le16">() == 0xBEEFu);
  assert(v.get<"be16">() == 0xCAFEu);
  assert(v.get<"n32">() == 0x11223344u);
  assert(v.get<"le32">() == 0xA1B2C3D4u);
  assert(v.get<"be32">() == 0x01020304u);
  assert(v.get<"guard">() == 0x5Au);

  // Validate stored byte order.
  // Layout byte offsets:
  // a:0, le16:1..2, be16:3..4, n32:5..8, le32:9..12, be32:13..16, guard:17
  expect_bytes(mem.data() + 0, {0x11});
  expect_bytes(mem.data() + 1, {0xEF, 0xBE}); // LE 0xBEEF
  expect_bytes(mem.data() + 3, {0xCA, 0xFE}); // BE 0xCAFE

  // native 32-bit is host dependent:
  if constexpr (std::endian::native == std::endian::little) {
    expect_bytes(mem.data() + 5, {0x44, 0x33, 0x22, 0x11});
  } else if constexpr (std::endian::native == std::endian::big) {
    expect_bytes(mem.data() + 5, {0x11, 0x22, 0x33, 0x44});
  } else {
    // Unusual mixed-endian machines: we can't state a byte order; at least assert roundtrip already passed.
    assert(true);
  }

  expect_bytes(mem.data() + 9,  {0xD4, 0xC3, 0xB2, 0xA1}); // LE 0xA1B2C3D4
  expect_bytes(mem.data() + 13, {0x01, 0x02, 0x03, 0x04}); // BE 0x01020304
  expect_bytes(mem.data() + 17, {0x5A});

  // Additional sanity: overwrite scalar fields and ensure bytes update exactly.
  v.set<"le16">(0x0001u);
  v.set<"be16">(0x0001u);
  expect_bytes(mem.data() + 1, {0x01, 0x00}); // LE
  expect_bytes(mem.data() + 3, {0x00, 0x01}); // BE
  assert(v.get<"le16">() == 1u);
  assert(v.get<"be16">() == 1u);

  return 0;
}
