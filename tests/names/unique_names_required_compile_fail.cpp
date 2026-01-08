#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/names/unique_names_required_compile_fail.cpp
//
// Contract (§15):
//   Field names are compile-time fixed_string NTTPs.
//   Within a single mad::packet<...>, all named fields must have UNIQUE names.
//   Duplicate names are ill-formed and must be rejected at compile time.
//
// This is a compile-fail test: it must NOT compile.
//
// Notes on coverage strategy:
//   - We include multiple duplicate-name constructions across different field kinds
//     to prevent "accidental uniqueness checks" that only cover a subset of kinds.
//   - Some compilers may stop after the first hard error; that's fine. The test harness
//     only requires compilation to fail.
//
// Expected failure site:
//   - the packet's internal name validation (static_assert) that enforces uniqueness.

namespace madpacket_names_compile_fail_unique {

using Sub = mad::packet<
  mad::u8<"x">,
  mad::u8<"y">
>;

// Case 1: Duplicate names on scalar integer fields.
using P1 = mad::packet<
  mad::u8<"dup">,
  mad::u16<"dup">
>;

 // Case 2: Duplicate name between bytes field and scalar integer field.
 using P2 = mad::packet<
  mad::bytes<"dup", 4>,
  mad::u32<"dup">
>;

 // Case 3: Duplicate name between subpacket field and scalar field.
 using P3 = mad::packet<
  mad::subpacket<Sub, "dup">,
  mad::u8<"dup">
>;

 // Case 4: Duplicate between bitfield and scalar with padding in between.
 using P4 = mad::packet<
  mad::u1<"a">,
  mad::pad_bits<7>,
  mad::ubits<13, "dup">,
  mad::pad_bits<3>,
  mad::u8<"dup">
>;

 // Case 5: Triple duplication (same name appears 3 times).
 using P5 = mad::packet<
  mad::u8<"dup3">,
  mad::u16<"dup3">,
  mad::u32<"dup3">
>;

 // Case 6: Duplicate names for signed and unsigned fields.
 using P6 = mad::packet<
  mad::i16<"sx">,
  mad::u16<"sx">
>;

 // Case 7: Duplicate between endian-tagged and native fields.
 using P7 = mad::packet<
  mad::le_u32<"e">,
  mad::be_u32<"e">
>;

 // Force instantiation/usage to ensure validation runs even if lazily instantiated.
// If the library validates at definition time, it will fail earlier.
 static constexpr std::size_t s1 = P1::total_bits;
static constexpr std::size_t s2 = P2::total_bits;
static constexpr std::size_t s3 = P3::total_bits;
static constexpr std::size_t s4 = P4::total_bits;
static constexpr std::size_t s5 = P5::total_bits;
static constexpr std::size_t s6 = P6::total_bits;
static constexpr std::size_t s7 = P7::total_bits;

 // Non-failing example (documented only): duplicates are only forbidden *within* a packet.
// Two different packet types may reuse names freely.
 #if 0
using OK1 = mad::packet<mad::u8<"a">>;
using OK2 = mad::packet<mad::u16<"a">>; // OK: different packet type
#endif

} // namespace madpacket_names_compile_fail_unique

int main() {
  // If compilation reaches here, uniqueness enforcement failed.
  return 0;
}
