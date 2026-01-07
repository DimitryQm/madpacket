#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// tests/bounds/make_view_asserts.cpp
//
// Contract (§9):
//   - make_view(data, n) uses MAD_ASSERT to enforce n >= Packet::total_bytes.
//   - direct construction view/cview from pointer is unchecked.
//   - if assertions are compiled out, the caller still must satisfy preconditions.
//
// This test *intercepts* MAD_ASSERT to avoid process termination and to prove that
// make_view routes through MAD_ASSERT. This keeps the test meaningful regardless
// of NDEBUG and without a unit-test framework.
//
// IMPORTANT: MAD_ASSERT must be defined before including madpacket.hpp.

static inline int g_assert_failures = 0;
static inline int g_assert_checks = 0;

#define MAD_ASSERT(x) do { ++g_assert_checks; if(!(x)) { ++g_assert_failures; } } while(0)
#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using namespace mad;

  using P = packet<
    u8<"a">,
    u16<"b">,
    ubits<5, "c">,
    pad_bits<3>,
    u8<"d">
  >;

  static_assert(P::total_bytes == 1 + 2 + 1 + 1);

  // Case A: make_view with sufficient buffer triggers MAD_ASSERT once (check) and no failure.
  {
    g_assert_checks = 0;
    g_assert_failures = 0;

    std::array<std::byte, P::total_bytes> buf{};
    auto v = make_view<P>(buf.data(), buf.size());

    // Should have checked exactly once.
    static_assert(std::is_same_v<decltype(v), view<P>>);
    static_assert(std::is_same_v<decltype(make_view<P>(static_cast<std::byte const*>(buf.data()), buf.size())), cview<P>>);

    // The mutable make_view check
    if (g_assert_checks != 1) return 1;
    if (g_assert_failures != 0) return 2;

    // Use the view to ensure it is usable after a passing check.
    v.set<"a">(0x12u);
    v.set<"b">(0xBEEFu);
    v.set<"c">(0x1Fu);
    v.set<"d">(0x34u);

    if (v.get<"a">() != 0x12u) return 3;
    if (v.get<"b">() != 0xBEEFu) return 4;
    if (v.get<"c">() != 0x1Fu) return 5;
    if (v.get<"d">() != 0x34u) return 6;
  }

  // Case B: make_view with insufficient buffer must trigger MAD_ASSERT failure.
  // We avoid using the returned view; the contract says behavior is undefined.
  {
    g_assert_checks = 0;
    g_assert_failures = 0;

    std::array<std::byte, P::total_bytes> buf{};
    // n is too small by 1.
    (void)make_view<P>(buf.data(), buf.size() - 1);

    // Should have checked once and failed once.
    if (g_assert_checks != 1) return 10;
    if (g_assert_failures != 1) return 11;

    // Repeat for const overload.
    g_assert_checks = 0;
    g_assert_failures = 0;
    (void)make_view<P>(static_cast<std::byte const*>(buf.data()), buf.size() - 1);
    if (g_assert_checks != 1) return 12;
    if (g_assert_failures != 1) return 13;
  }

  // Case C: direct view construction is unchecked (no MAD_ASSERT calls)
  //
  // We prove "unchecked" by observing that our MAD_ASSERT hook is not called.
  // (Note: this does NOT mean the operation is safe for short buffers; it isn't.)
  {
    g_assert_checks = 0;
    g_assert_failures = 0;

    std::array<std::byte, 1> tiny{std::byte{0}};
    view<P> v(tiny.data());                 // unchecked
    cview<P> cv(static_cast<std::byte const*>(tiny.data())); // unchecked

    if (g_assert_checks != 0) return 20;
    if (g_assert_failures != 0) return 21;

    // Do not dereference fields here; buffer is too small for P and that would be UB.
    (void)v;
    (void)cv;
    (void)u8;
  }

  // Case D: make_view boundary condition exactly equal to total_bytes.
  {
    g_assert_checks = 0;
    g_assert_failures = 0;

    std::array<std::byte, P::total_bytes> buf{};
    auto v = make_view<P>(buf.data(), P::total_bytes);
    if (g_assert_checks != 1) return 30;
    if (g_assert_failures != 0) return 31;

    v.set<"a">(0xFFu);
    if (v.get<"a">() != 0xFFu) return 32;
  }

  return 0;
}
