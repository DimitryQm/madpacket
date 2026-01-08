#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/api/reject_set_on_bytes_compile_fail.cpp
//
// Contract (§14):
//   Bytes fields (bytes_field / mad::bytes<Name,N>) do not support set<Name>(...)
//   because "setting" a byte region is ambiguous. The caller must obtain the
//   bytes view via get<Name>() and then mutate the returned span/pointer.
//   Attempting to call set on a bytes field is ill-formed and must be rejected
//   at compile time.
//
// Expected failure mechanism in madpacket.hpp (set_impl):
//   if constexpr (F::kind == field_kind::bytes) {
//     static_assert(sizeof(F) == 0, "bytes field: use get<name>().as_span() / .data() to write");
//   }
//
// This is a compile-fail test: it must NOT compile.

namespace madpacket_api_compile_fail_set_bytes {

using P = mad::packet<
  mad::u8<"pre">,             // index 0
  mad::bytes<"payload", 4>,   // index 1 (bytes field)
  mad::be_u16<"post">         // index 2
>;

static_assert(P::field_count == 3);
static_assert(P::total_bytes == 1 + 4 + 2);

inline mad::view<P> make_mut_view(std::byte* p) {
  return mad::make_view<P>(p, P::total_bytes);
}

// Case 1: name-based set() on a bytes field must fail.
inline void should_fail_name_set_on_bytes() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  // Allowed:
  v.set<"pre">(0x12u);
  v.set<"post">(0xBEEFu);

  // Forbidden: bytes field does not accept set.
  v.set<"payload">(0u); // expected: "... bytes field: use get<name>().as_span() / .data() to write"
}

// Case 2: index-based set_i() on a bytes field must fail too.
inline void should_fail_index_set_i_on_bytes() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  // Index 1 is payload (bytes field).
  v.template set_i<1>(0u); // expected compile-time failure
}


// Case 2b: set() should be rejected regardless of the argument type.
// (The bytes field has no defined "assignment" semantics; you must mutate via get().)
inline void should_fail_set_bytes_with_various_argument_types() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  // All of these must be rejected at compile time for the same reason.
  v.set<"payload">(nullptr);
  v.set<"payload">(buf.data());
  v.set<"payload">(std::array<std::byte, 4>{});
  v.set<"payload">(std::uint64_t{0xDEADBEEF});
}

// Case 2c: index-based set_i() should also reject all argument types.
inline void should_fail_set_i_bytes_with_various_argument_types() {
  alignas(8) std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  v.template set_i<1>(nullptr);
  v.template set_i<1>(buf.data());
  v.template set_i<1>(std::array<std::byte, 4>{});
  v.template set_i<1>(std::uint64_t{0x0123456789ABCDEFull});
}

// Case 3: demonstrate correct usage (excluded from compilation in this compile-fail TU).
#if 0
inline void should_compile_correct_bytes_mutation() {
  std::array<std::byte, P::total_bytes> buf{};
  auto v = make_mut_view(buf.data());

  v.set<"pre">(0x12u);
  auto bytes = v.get<"payload">();       // bytes_view<4>
  auto sp = bytes.as_span();             // std::span<std::byte,4>
  sp[0] = std::byte{0xDE};
  sp[1] = std::byte{0xAD};
  sp[2] = std::byte{0xBE};
  sp[3] = std::byte{0xEF};
  v.set<"post">(0xBEEFu);
}
#endif

} // namespace madpacket_api_compile_fail_set_bytes

int main() {
  using namespace madpacket_api_compile_fail_set_bytes;

  // Any one failure is sufficient. Include both to verify both APIs reject it.
  should_fail_name_set_on_bytes();
  should_fail_index_set_i_on_bytes();
  should_fail_set_bytes_with_various_argument_types();
  should_fail_set_i_bytes_with_various_argument_types();

  return 0;
}
