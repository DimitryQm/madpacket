#include <array>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/api/index_out_of_range_compile_fail.cpp  (compile-fail)
//
// Views (§3.3):
//   get_i<I>() / set_i<I>(...) require I < Packet::field_count.
//   If I is out of range, the call is ill-formed (static_assert fails).
//
// This file must FAIL TO COMPILE by instantiating out-of-range index access.
//
// Notes on tooling:
//   - Some test runners compile these files expecting failure.
//   - They usually don't execute anything; the point is the compile-time rejection.
//   - Keep the out-of-range access in an unconditionally-instantiated context
//     (like main), so optimizers cannot discard it.
//

using namespace mad;

using P = packet<
  u8<"a">,
  u16<"b">,
  u3<"c">,
  pad_bits<5>,   // keep a pad inside to ensure field_count isn't "just scalars"
  u8<"d">
>;

static_assert(P::field_count == 5);

int main() {
  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_view<P>(buf.data(), buf.size());

  // In-range sanity (should compile if this file didn't intentionally fail later).
  (void)v.get_i<0>();
  v.set_i<0>(0x12u);

  // Out-of-range access: must hard-fail at compile time.
  // The library enforces: static_assert(I < Packet::field_count).
  (void)v.get_i<P::field_count>();      // <-- should fail
  v.set_i<P::field_count>(0u);          // (likely won't be reached)
  return 0;
}


/*
Extra checks (kept here as documentation + optional additional compile-fail triggers).

The view also forbids accessing pad fields through get_i/set_i, just like get/set by name.
That is tested for name-based access in tests/api/reject_get_set_on_pad_compile_fail.cpp.
If you want symmetric coverage for index-based pad access, you can add a dedicated compile-fail
file (recommended), or temporarily enable the block below.

We keep this disabled to avoid producing multiple unrelated diagnostics in one compile-fail TU.
*/

#if 0
int pad_access_should_fail() {
  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_view<P>(buf.data(), buf.size());
  // Index 3 is pad_bits<5> in P above.
  (void)v.get_i<3>();   // should fail: "cannot get a pad field"
  return 0;
}
#endif

#if 0
int const_index_set_should_fail() {
  std::array<std::byte, P::total_bytes> buf{};
  auto cv = make_view<P>(static_cast<std::byte const*>(buf.data()), buf.size());
  // set_i should be ill-formed on cview as well (static_assert(Mutable)).
  cv.set_i<0>(0u);
  return 0;
}
#endif


// Expected diagnostic (approximate)
// You should see a failure originating from something like:
//
//   static_assert(I < Packet::field_count);
//
// This is a structural API guard: index-based access is a TMP-friendly fast path,
// and out-of-range indices must be a hard compile-time error.

