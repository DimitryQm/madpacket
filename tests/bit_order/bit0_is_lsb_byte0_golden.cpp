#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

  #include "madpacket.hpp"


// Golden test: bit 0 is LSB of byte 0 (little-endian bit numbering model).

int main() {
  using namespace mad;

  // b0 at bit offset 0, b8 at bit offset 8 (LSB of byte 1).
  using P = packet<
    u1<"b0">,
    pad_bits<7>,
    u1<"b8">
  >;

  static_assert(P::total_bits == 9);
  static_assert(P::total_bytes == 2);

  {
    std::array<std::byte, P::total_bytes> buf{std::byte{0}, std::byte{0}};
    auto v = make_view<P>(buf.data(), buf.size());

    v.set<"b0">(1);
    assert(std::to_integer<unsigned>(buf[0]) == 0x01u);
    assert(std::to_integer<unsigned>(buf[1]) == 0x00u);

    v.set<"b0">(0);
    assert(std::to_integer<unsigned>(buf[0]) == 0x00u);
    assert(std::to_integer<unsigned>(buf[1]) == 0x00u);

    v.set<"b8">(1);
    assert(std::to_integer<unsigned>(buf[0]) == 0x00u);
    assert(std::to_integer<unsigned>(buf[1]) == 0x01u);

    // Also verify get sees the same mapping.
    assert(v.get<"b0">() == 0u);
    assert(v.get<"b8">() == 1u);
  }

  // b7 at bit offset 7: MSB of byte 0.
  using Q = packet<
    pad_bits<7>,
    u1<"b7">
  >;

  static_assert(Q::total_bits == 8);
  static_assert(Q::total_bytes == 1);

  {
    std::array<std::byte, Q::total_bytes> buf{std::byte{0}};
    auto v = make_view<Q>(buf.data(), buf.size());

    v.set<"b7">(1);
    assert(std::to_integer<unsigned>(buf[0]) == 0x80u);

    v.set<"b7">(0);
    assert(std::to_integer<unsigned>(buf[0]) == 0x00u);

    buf[0] = std::byte{0x80};
    assert(v.get<"b7">() == 1u);

    buf[0] = std::byte{0x01};
    assert(v.get<"b7">() == 0u);
  }

  // Bits increase within a byte from LSB to MSB.
  using R = packet<
    u1<"b0">, u1<"b1">, u1<"b2">, u1<"b3">,
    u1<"b4">, u1<"b5">, u1<"b6">, u1<"b7">
  >;

  static_assert(R::total_bits == 8);
  static_assert(R::total_bytes == 1);

  {
    std::array<std::byte, R::total_bytes> buf{std::byte{0}};
    auto v = make_view<R>(buf.data(), buf.size());

    v.set<"b1">(1);
    assert(std::to_integer<unsigned>(buf[0]) == 0x02u);

    buf[0] = std::byte{0x00};
    v.set<"b3">(1);
    assert(std::to_integer<unsigned>(buf[0]) == 0x08u);

    buf[0] = std::byte{0x00};
    v.set<"b7">(1);
    assert(std::to_integer<unsigned>(buf[0]) == 0x80u);

    // sanity: write raw and read back
    buf[0] = std::byte{0x55}; // 01010101b
    assert(v.get<"b0">() == 1u);
    assert(v.get<"b1">() == 0u);
    assert(v.get<"b2">() == 1u);
    assert(v.get<"b3">() == 0u);
    assert(v.get<"b4">() == 1u);
    assert(v.get<"b5">() == 0u);
    assert(v.get<"b6">() == 1u);
    assert(v.get<"b7">() == 0u);
  }

  return 0;
}
