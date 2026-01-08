#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/mmio/bitfield_bus_word_le_stream.cpp
//
// Contract (§3, §10):
//   For MMIO views, when a bitfield integer field is fully contained within a single
//   bus word, the implementation reads that bus word and interprets it as a
//   "little-endian byte-stream numeric" before extracting bits.
//
//   Concretely, the bus word value is assembled as:
//     word = sum_{i=0..bus_bytes-1} (byte[i] << (8*i))
//   regardless of host endianness.
//
// This test is a GOLDEN mapping check for that rule. On little-endian hosts it will
// pass even if an incorrect implementation uses native-endian typed loads; on big-endian
// hosts it will fail if the code is host-endian dependent.
//
// Even on little-endian hosts, it still provides coverage that the bit numbering inside
// the word matches the contract (LSB=bit0 of byte0, increasing with address).

static inline std::uint64_t load_le_n(std::byte const* p, std::size_t n) {
  std::uint64_t x = 0;
  for (std::size_t i = 0; i < n; ++i) {
    x |= (std::uint64_t(std::to_integer<unsigned>(p[i])) << (8u * i));
  }
  return x;
}

static inline void store_bytes(std::byte* p, std::initializer_list<unsigned> bytes) {
  std::size_t i = 0;
  for (unsigned b : bytes) p[i++] = std::byte{static_cast<unsigned char>(b)};
}

static inline std::uint64_t mask_bits(unsigned bits) {
  return bits >= 64 ? ~0ull : ((1ull << bits) - 1ull);
}

int main() {
  using Bus = mad::reg::bus32; // 32-bit bus word, 4 bytes
  static_assert(Bus::bytes == 4);
  static_assert(Bus::bits == 32);

  // Packet with multiple bitfields inside the *same* bus word.
  //
  // We choose offsets that exercise:
  //   - starting at bit 0
  //   - starting mid-byte
  //   - spanning across bytes
  using P = mad::packet<
    mad::ubits<5,  "b0_5">,   // bits [0..5)
    mad::ubits<11, "b5_11">,  // bits [5..16)
    mad::ubits<7,  "b16_7">,  // bits [16..23)
    mad::ubits<9,  "b23_9">,  // bits [23..32) => ends at bit 32 (still within one bus word)
    mad::u8<"guard">          // next byte, outside the first bus word
  >;

  static_assert(P::total_bytes == 4 + 1); // 32 bits + 8 bits

  // Backing storage with known bytes.
  alignas(Bus::align) std::array<std::byte, P::total_bytes> mem{};
  store_bytes(mem.data(), {0x01, 0x23, 0x45, 0x67, 0xAA});

  // Bind as MMIO view.
  volatile std::byte* vbase = reinterpret_cast<volatile std::byte*>(mem.data());
  auto v = mad::reg::make_view<P, Bus>(reinterpret_cast<volatile void*>(vbase));

  // Assemble first bus word using the contract LE byte-stream numeric model.
  const std::uint64_t w0 = load_le_n(mem.data(), Bus::bytes);
  assert(w0 == 0x67452301ull); // little-endian byte stream numeric

  // Compute expected bitfield values from w0.
  auto expect = [&](unsigned off, unsigned bits) -> std::uint64_t {
    return (w0 >> off) & mask_bits(bits);
  };

  // Offsets per layout:
  //   b0_5:  off=0  bits=5
  //   b5_11: off=5  bits=11
  //   b16_7: off=16 bits=7
  //   b23_9: off=23 bits=9
  const std::uint64_t e0 = expect(0, 5);
  const std::uint64_t e1 = expect(5, 11);
  const std::uint64_t e2 = expect(16, 7);
  const std::uint64_t e3 = expect(23, 9);

  assert(v.get<"b0_5">() == e0);
  assert(v.get<"b5_11">() == e1);
  assert(v.get<"b16_7">() == e2);
  assert(v.get<"b23_9">() == e3);

  // Guard byte should be untouched by any reads.
  assert(std::to_integer<unsigned>(mem[4]) == 0xAAu);

  // Also validate a write through the one-word RMW path doesn't disturb the numeric model:
  // We'll set b5_11 and check the resulting bus word matches the LE-bit update.
  const std::uint64_t new_v = 0x3ABu; // 11-bit value (0b1110101011)
  v.set<"b5_11">(new_v);

  // Expected updated word:
  const std::uint64_t m = mask_bits(11) << 5;
  const std::uint64_t w1 = (w0 & ~m) | ((new_v & mask_bits(11)) << 5);

  const std::uint64_t w_mem_after = load_le_n(mem.data(), Bus::bytes);
  assert(w_mem_after == (w1 & 0xFFFFFFFFull));

  // Reads must now reflect the updated value; other fields preserve their bits.
  assert(v.get<"b5_11">() == (new_v & mask_bits(11)));
  assert(v.get<"b0_5">() == (w1 >> 0)  & mask_bits(5));
  assert(v.get<"b16_7">() == (w1 >> 16) & mask_bits(7));
  assert(v.get<"b23_9">() == (w1 >> 23) & mask_bits(9));

  // Guard still untouched by this set (bitfield is within the first bus word).
  assert(std::to_integer<unsigned>(mem[4]) == 0xAAu);

  return 0;
}
