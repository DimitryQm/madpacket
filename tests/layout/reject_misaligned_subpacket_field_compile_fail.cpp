#include <cstddef>

#include "madpacket.hpp"

// tests/layout/reject_misaligned_subpacket_field_compile_fail.cpp
//
// Contract (layout):
//   subpacket fields (mad::subpacket<Packet, Name>) use (bit_offset >> 3) as the
//   base pointer for the nested view.
//   Therefore a subpacket MUST start on a byte boundary: bit_offset % 8 == 0.
//
// Expected failure mechanism in madpacket.hpp (packet<...>::validate()):
//   static_assert((offset_bits % 8) == 0,
//     "bytes/subpacket fields must start on a byte boundary ...");
//
// This is a compile-fail test: it must NOT compile.

namespace madpacket_layout_compile_fail_misaligned_subpacket {

using Inner = mad::packet<
  mad::u8<"x">,
  mad::u8<"y">
>;

// Put a 1-bit field first so the subpacket starts at bit offset 1.
using Bad = mad::packet<
  mad::u1<"b0">,
  mad::subpacket<Inner, "inner">
>;

// Force instantiation / completeness so the validate() static_assert triggers.
static_assert(Bad::field_count == 2);

// If you want a "good" reference (should compile), this is the aligned version:
#if 0
using Good = mad::packet<
  mad::u8<"b0">,
  mad::subpacket<Inner, "inner">
>;
static_assert(Good::total_bytes == 1 + Inner::total_bytes);
#endif

} // namespace madpacket_layout_compile_fail_misaligned_subpacket

int main() { return 0; }
