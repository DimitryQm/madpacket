#include <cstddef>

#include "madpacket.hpp"

// tests/layout/reject_misaligned_bytes_field_compile_fail.cpp
//
// Contract (layout):
//   bytes fields (mad::bytes<Name, N>) are addressed using (bit_offset >> 3).
//   Therefore a bytes field MUST start on a byte boundary: bit_offset % 8 == 0.
//
// Expected failure mechanism in madpacket.hpp (packet<...>::validate()):
//   static_assert((offset_bits % 8) == 0,
//     "bytes/subpacket fields must start on a byte boundary ...");
//
// This is a compile-fail test: it must NOT compile.

namespace madpacket_layout_compile_fail_misaligned_bytes {

// Put a 1-bit field first so the next field starts at bit offset 1.
using Bad = mad::packet<
  mad::u1<"b0">,
  mad::bytes<"payload", 4>
>;

// Force instantiation / completeness so the validate() static_assert triggers.
static_assert(Bad::field_count == 2);

// If you want a "good" reference (should compile), this is the aligned version:
#if 0
using Good = mad::packet<
  mad::u8<"b0">,
  mad::bytes<"payload", 4>
>;
static_assert(Good::total_bytes == 1 + 4);
#endif

} // namespace madpacket_layout_compile_fail_misaligned_bytes

int main() { return 0; }
