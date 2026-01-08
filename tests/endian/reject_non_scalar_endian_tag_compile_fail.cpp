#include <array>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/endian/reject_non_scalar_endian_tag_compile_fail.cpp
//
// Contract (§6):
//   Endianness tags (little/big) are only legal for *byte-aligned scalar widths*
//   exactly in {8,16,32,64}.
//
//   Any integer field that is not a "byte-aligned scalar integer field" in the
//   implementation’s sense is treated as a "bitfield integer field" and MUST use
//   native endianness. That includes:
//     - widths not in {8,16,32,64} even when byte-aligned (e.g. 12-bit, 24-bit)
//     - byte-aligned scalar widths that are NOT byte-aligned due to bit offset shift != 0
//
//   A non-native endian tag (little_endian_t / big_endian_t) on such a field is
//   ill-formed and must be rejected via static_assert in the access path.
//
// This is a compile-fail test: it must NOT compile.
//
// Implementation note:
//   Many libraries validate endianness tags at type-definition time.
//   madpacket validates it in the accessor path (get/set), so this TU forces
//   instantiation by calling get/set on the offending fields.

namespace madpacket_endian_compile_fail_non_scalar {

// Case 1: 24-bit (byte-aligned) is NOT a scalar-width field => must not accept little_endian.

  using P24 = mad::packet<
  mad::int_field<"x24", 24, false, mad::little_endian_t>,
  mad::u8<"tail">
>;
static_assert(P24::total_bytes == 3 + 1);

inline void trigger_24bit_endian_reject() {
  alignas(8) std::array<std::byte, P24::total_bytes> buf{};
  auto v = mad::make_view<P24>(buf.data(), buf.size());

  // Must fail compilation due to illegal endian tag on a bitfield-style access.
  (void)v.get<"x24">(); // expected compile-time failure
}

// Case 2: 16-bit width but MISALIGNED (shift != 0) => treated as bitfield => endian tag illegal.
using Pmis16 = mad::packet<
  mad::u1<"pad1">,
  mad::int_field<"x16mis", 16, false, mad::big_endian_t>, // starts at bit 1
  mad::u7<"tail7">
>;
static_assert(Pmis16::total_bytes == 3); // 1 + 16 + 7 = 24 bits

inline void trigger_misaligned_16bit_endian_reject() {
  alignas(8) std::array<std::byte, Pmis16::total_bytes> buf{};
  auto v = mad::make_view<Pmis16>(buf.data(), buf.size());

  // Both get and set must be ill-formed; include both to hit both access paths.
  (void)v.get<"x16mis">();     // expected compile-time failure
  v.set<"x16mis">(0xBEEFu);    // expected compile-time failure
}

// Case 3: 12-bit explicitly endian-tagged => illegal.
using P12 = mad::packet<
  mad::int_field<"x12", 12, false, mad::little_endian_t>,
  mad::u4<"tail4">
>;
static_assert(P12::total_bytes == 2); // 12 + 4 = 16 bits

inline void trigger_12bit_endian_reject() {
  alignas(8) std::array<std::byte, P12::total_bytes> buf{};
  auto v = mad::make_view<P12>(buf.data(), buf.size());
  v.set<"x12">(0xFFFu); // expected compile-time failure
}

// Case 4: 64-bit scalar width but MISALIGNED (shift != 0) => treated as bitfield => endian tag illegal.
// This is important because some implementations might accidentally allow endian tags
// solely based on width, forgetting about alignment/shift.
using Pmis64 = mad::packet<
  mad::u3<"pad3">,
  mad::int_field<"x64mis", 64, false, mad::little_endian_t>, // starts at bit 3
  mad::u5<"tail5">
>;
static_assert(Pmis64::total_bits == 3 + 64 + 5);
static_assert(Pmis64::total_bytes == 9); // 72 bits

inline void trigger_misaligned_64bit_endian_reject() {
  alignas(16) std::array<std::byte, Pmis64::total_bytes> buf{};
  auto v = mad::make_view<Pmis64>(buf.data(), buf.size());

  (void)v.get<"x64mis">();          // expected compile-time failure
  v.set<"x64mis">(0x0123456789ABCDEFull); // expected compile-time failure
}

// Case 5: 8-bit width but MISALIGNED (shift != 0). Even though 8-bit is scalar width,
// misalignment makes it a bitfield path, so non-native endian tags must be rejected.
using Pmis8 = mad::packet<
  mad::u4<"pad4">,
  mad::int_field<"x8mis", 8, false, mad::big_endian_t>, // starts at bit 4
  mad::u4<"tail4">
>;
static_assert(Pmis8::total_bits == 16);
static_assert(Pmis8::total_bytes == 2);

inline void trigger_misaligned_8bit_endian_reject() {
  alignas(8) std::array<std::byte, Pmis8::total_bytes> buf{};
  auto v = mad::make_view<Pmis8>(buf.data(), buf.size());

  // Accessor instantiation should reject the non-native endian tag.
  (void)v.get<"x8mis">(); // expected compile-time failure
}

} // namespace madpacket_endian_compile_fail_non_scalar

int main() {
  using namespace madpacket_endian_compile_fail_non_scalar;

  trigger_24bit_endian_reject();
  trigger_misaligned_16bit_endian_reject();
  trigger_12bit_endian_reject();
  trigger_misaligned_64bit_endian_reject();
  trigger_misaligned_8bit_endian_reject();

  return 0;
}
