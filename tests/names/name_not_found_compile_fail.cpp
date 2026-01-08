#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/names/name_not_found_compile_fail.cpp
//
// Contract (§15):
//   get<Name>() / set<Name>(...) are compile-time name lookups.
//   If Name does not exist in the packet, the program is ill-formed and must be rejected.
//
// This is a compile-fail test: it must NOT compile.
//
// We trigger failures for:
//   - completely missing names
//   - similar-but-different names (case, whitespace, typos)
//   - forcing the name-lookup at type-level (field_by_name_t) in addition to get/set

namespace madpacket_names_compile_fail_not_found {

using P = mad::packet<
  mad::u8<"a">,
  mad::le_u16<"b">,
  mad::ubits<5, "c">,
  mad::pad_bits<3>,
  mad::bytes<"payload", 2>,
  mad::subpacket<mad::packet<mad::u8<"x">>, "sub"> // extra named field for realism
>;

static_assert(P::total_bytes >= 1 + 2 + 1 + 2 + 1);

inline mad::view<P> make_mut() {
  static std::array<std::byte, P::total_bytes> buf{};
  return mad::make_view<P>(buf.data(), buf.size());
}

inline void trigger_get_name_not_found() {
  auto v = make_mut();

  // Valid lookups:
  (void)v.get<"a">();
  (void)v.get<"b">();
  (void)v.get<"c">();
  (void)v.get<"payload">();
  (void)v.get<"sub">();

  // Invalid lookup:
  (void)v.get<"nope">(); // expected compile-time failure: field name not found
}

inline void trigger_set_name_not_found() {
  auto v = make_mut();

  // Invalid set:
  v.set<"missing">(123u); // expected compile-time failure
}

inline void trigger_similar_name_not_found() {
  auto v = make_mut();

  // No fuzzy matching: all of these must fail.
  (void)v.get<"Payload">(); // case differs
  v.set<"a ">(0u);          // trailing space differs
  (void)v.get<"payloa">();  // typo differs
  (void)v.get<"sub " >();   // subtle whitespace differs
}

// Type-level forcing: request a field type for a name that doesn't exist.
// This should also fail (either via a static_assert or by forming an invalid type).
template <mad::fixed_string Name>
struct force_field_by_name {
  using type = mad::detail::field_by_name_t<P, Name>;
};

inline void trigger_type_level_name_not_found() {
  // This line should be ill-formed during template instantiation:
  using Bad = typename force_field_by_name<"nope">::type; // expected compile-time failure
  (void)sizeof(Bad);
}

// Also validate that "index_of" reports "not found" for a missing name (if exposed),
// and that users are not expected to handle it manually; get/set hard-reject instead.
inline void trigger_index_of_contract() {
  constexpr std::size_t idx_ok = P::template index_of<"a">;
  static_assert(idx_ok != static_cast<std::size_t>(-1));

  constexpr std::size_t idx_bad = P::template index_of<"nope">;
  static_assert(idx_bad == static_cast<std::size_t>(-1), "missing name must map to -1");

  // Even though index_of returns -1, get/set must still be ill-formed rather than UB.
  auto v = make_mut();
  (void)v.get<"nope">(); // expected compile-time failure
}

} // namespace madpacket_names_compile_fail_not_found

int main() {
  using namespace madpacket_names_compile_fail_not_found;

  trigger_get_name_not_found();
  trigger_set_name_not_found();
  trigger_similar_name_not_found();
  trigger_type_level_name_not_found();
  trigger_index_of_contract();

  return 0;
}
