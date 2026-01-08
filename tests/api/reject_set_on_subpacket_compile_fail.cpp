#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/api/reject_set_on_subpacket_compile_fail.cpp
//
// Contract (§14):
//   Subpacket fields (subpacket_field / mad::subpacket<Packet,Name>) do not support
//   set<Name>(...) because assignment would imply copying/serializing an entire
//   nested packet. Instead, you obtain a nested view via get<Name>() and then
//   operate on that nested view.
//
// Attempting to call set on a subpacket field is ill-formed and must be rejected
// at compile time.
//
// Expected failure mechanism in madpacket.hpp (set_impl):
//   else if constexpr (F::kind == field_kind::subpacket) {
//     static_assert(sizeof(F) == 0, "subpacket field: assign via nested view");
//   }
//
// This is a compile-fail test: it must NOT compile.

namespace madpacket_api_compile_fail_set_subpacket {

using Sub = mad::packet<
  mad::u16<"x">,
  mad::u8<"y">,
  mad::ubits<3, "z">,
  mad::pad_bits<5>
>;

static_assert(Sub::total_bytes == 2 + 1 + 1);

using P = mad::packet<
  mad::u8<"pre">,               // index 0
  mad::subpacket<Sub, "sub">,   // index 1 (subpacket field)
  mad::le_u32<"post">           // index 2
>;

static_assert(P::field_count == 3);
static_assert(P::total_bytes == 1 + Sub::total_bytes + 4);

inline mad::view<P> make_mut_view(std::byte* p) {
  return mad::make_view<P>(p, P::total_bytes);
}

// Case 1: name-based set() on subpacket must fail.
inline void should_fail_name_set_on_subpacket() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  v.set<"pre">(0x12u);
  v.set<"post">(0x11223344u);

  // Forbidden: cannot assign to a subpacket field via set.
  v.set<"sub">(0u); // expected compile-time failure: "subpacket field: assign via nested view"
}

// Case 2: index-based set_i() on subpacket must fail too.
inline void should_fail_index_set_i_on_subpacket() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  // Index 1 is the subpacket field.
  v.template set_i<1>(0u); // expected compile-time failure
}


// Case 2b: set() should be rejected even if the argument looks like a "subpacket".
inline void should_fail_set_subpacket_with_sub_like_arguments() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  auto s = v.get<"sub">(); // nested view (this is the correct way to interact)
  (void)s;

  // The following are still forbidden: set<"sub"> is not an assignment API.
  v.set<"sub">(s);
  v.set<"sub">(std::uint64_t{0});      // nonsense argument type; should still be rejected by the subpacket static_assert
}

// Case 3: document correct usage (excluded from compilation in this compile-fail TU).
#if 0
inline void should_compile_correct_nested_usage() {
  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  v.set<"pre">(0x12u);
  auto s = v.get<"sub">(); // mad::view<Sub> nested
  s.set<"x">(0xBEEFu);
  s.set<"y">(0xAAu);
  s.set<"z">(0x5u);
  v.set<"post">(0x11223344u);
}
#endif

} // namespace madpacket_api_compile_fail_set_subpacket

int main() {
  using namespace madpacket_api_compile_fail_set_subpacket;

  // Any one failure is enough; keep both for coverage.
  should_fail_name_set_on_subpacket();
  should_fail_index_set_i_on_subpacket();
  should_fail_set_subpacket_with_sub_like_arguments();

  return 0;
}
