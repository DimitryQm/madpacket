#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/api/reject_set_on_const_view_compile_fail.cpp
//
// Contract (§14):
//   Attempting to call set<Name>(...) (or set_i<I>(...)) on a const view type
//   (mad::cview<Packet>) is ill-formed and must be rejected at compile time.
//
// This is a compile-fail test: it must NOT compile.
//
// Expected failure mechanism in madpacket.hpp:
//   mad::detail::set_impl<Packet, Mutable=false, ...> contains
//     static_assert(Mutable, "attempting to set on const view");
//
// We trigger this in multiple ways to ensure both name-based and index-based
// APIs reject mutation through a const view.

namespace madpacket_api_compile_fail_const_set {

using P = mad::packet<
  mad::u8<"a">,
  mad::le_u16<"b">,
  mad::ubits<5, "c">,
  mad::pad_bits<3>,
  mad::u32<"d">
>;

static_assert(P::total_bytes == 1 + 2 + 1 + 4);

// Helper: produce a cview in a way users will actually do (from const pointer).
inline mad::cview<P> make_const_view(std::byte const* p) {
  // make_view returns cview when given a const pointer.
  return mad::make_view<P>(p, P::total_bytes);
}

// Case 1: name-based set() on cview must hard-fail.
inline void should_fail_name_set_on_cview() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto cv = make_const_view(static_cast<std::byte const*>(buf.data()));

  // get() is allowed:
  (void)cv.get<"a">();
  (void)cv.get<"b">();
  (void)cv.get<"c">();
  (void)cv.get<"d">();

  // set() is forbidden and must fail compilation:
  cv.set<"a">(0x12u); // expected: static_assert(Mutable, "attempting to set on const view");
}

// Case 2: index-based set_i() on cview must hard-fail.
inline void should_fail_index_set_i_on_cview() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto cv = make_const_view(static_cast<std::byte const*>(buf.data()));

  // get_i is allowed:
  (void)cv.template get_i<0>();
  (void)cv.template get_i<1>();
  (void)cv.template get_i<2>();
  (void)cv.template get_i<4>();

  // set_i is forbidden and must fail compilation:
  cv.template set_i<1>(0xBEEFu); // expected compile-time failure
}

// Case 3: direct cview construction + set must also fail.
inline void should_fail_direct_cview_set() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  mad::cview<P> cv(static_cast<std::byte const*>(buf.data()));

  // Any mutation attempt must fail:
  cv.set<"d">(0x11223344u); // expected compile-time failure
}

// Case 4: demonstrate that the mutable view *does* allow set (not compiled in this file).
#if 0
inline void should_compile_mutable_set() {
  std::array<std::byte, P::total_bytes> buf{};
  auto v = mad::make_view<P>(buf.data(), buf.size());
  v.set<"a">(0x12u);
  v.set<"b">(0xBEEFu);
  v.set<"c">(0x1Fu);
  v.set<"d">(0x11223344u);
}
#endif

} // namespace madpacket_api_compile_fail_const_set

int main() {
  using namespace madpacket_api_compile_fail_const_set;

  // Any one of these is sufficient to fail compilation.
  should_fail_name_set_on_cview();

  // Keeping additional triggers here increases diagnostic coverage (still compile-fail).
  should_fail_index_set_i_on_cview();
  should_fail_direct_cview_set();

  return 0;
}
