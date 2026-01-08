#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/api/reject_get_set_on_pad_compile_fail.cpp
//
// Contract (§14):
//   Pad fields (pad_bits / pad_bytes) are not addressable.
//   Attempting to get or set a pad field is ill-formed and must be rejected
//   at compile time.
//
// In this library, pad fields do not have names, so name-based get/set
// cannot even refer to them. The API still exposes indexed access
// (get_i<I>, set_i<I>) for TMP-heavy codegen, so the contract requires
// these operations to be rejected for pad fields.
//
// Expected failure mechanism in madpacket.hpp (view_base):
//   - get_i<I> has: static_assert(F::kind != field_kind::pad, "cannot get a pad field");
//   - set_i<I> has: static_assert(F::kind != field_kind::pad, "cannot set a pad field");
//
// This is a compile-fail test: it will NOT compile.

namespace madpacket_api_compile_fail_pad_access {

using P = mad::packet<
  mad::u8<"a">,          // index 0
  mad::pad_bits<5>,      // index 1 (pad)
  mad::ubits<3, "x">,    // index 2
  mad::pad_bytes<2>,     // index 3 (pad)
  mad::u16<"b">          // index 4
>;

static_assert(P::field_count == 5);
static_assert(P::total_bytes == 6);

inline mad::view<P> make_mut_view(std::byte* p) {
  return mad::make_view<P>(p, P::total_bytes);
}

inline void should_fail_get_pad_by_index() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  // Non-pad indexed gets compile fine:
  (void)v.template get_i<0>();
  (void)v.template get_i<2>();
  (void)v.template get_i<4>();

  // Pad indexed get must be rejected:
  (void)v.template get_i<1>(); // expected: "cannot get a pad field"
}

inline void should_fail_set_pad_by_index() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  // Non-pad indexed sets compile fine:
  v.template set_i<0>(0x12u);
  v.template set_i<2>(0x7u);
  v.template set_i<4>(0xBEEFu);

  // Pad indexed set must be rejected:
  v.template set_i<3>(0u); // expected: "cannot set a pad field"
}

inline void should_fail_mixed_pad_access() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  // Even attempting to *read* a pad field via get_i must fail.
  // And attempting to *write* a pad field via set_i must fail.
  (void)v.template get_i<1>(); // fail
  v.template set_i<3>(0);      // fail
}

// Document the proper idiom: pads exist only to create spacing in layout.
#if 0
inline void should_compile_non_pad_access_only() {
  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  v.set<"a">(0x12u);
  v.set<"x">(0x7u);
  v.set<"b">(0xBEEFu);
  // No operations on pad fields.
}
#endif

} // namespace madpacket_api_compile_fail_pad_access

int main() {
  using namespace madpacket_api_compile_fail_pad_access;

  // Any one failure is enough; we include multiple for coverage.
  should_fail_get_pad_by_index();
  should_fail_set_pad_by_index();
  should_fail_mixed_pad_access();

  return 0;
}
