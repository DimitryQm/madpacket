#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

// tests/mmio/make_view_basealign_asserts.cpp
//
// MMIO doc (§5):
//   mad::reg::make_view performs a MAD_ASSERT that the base pointer is aligned
//   to BaseAlign. This is a debug assertion (can be compiled out) but it must
//   exist and must check the exact BaseAlign bitmask condition.
//
// This test overrides MAD_ASSERT so we can *observe* whether the check is
// executed without aborting the process.

namespace test {
  inline int assert_calls = 0;
  inline int assert_failed = 0;

  inline void reset() noexcept {
    assert_calls = 0;
    assert_failed = 0;
  }

  inline void record(bool ok) noexcept {
    ++assert_calls;
    if (!ok) ++assert_failed;
  }
} // namespace test

#define MAD_ASSERT(x) ::test::record(!!(x))

#include "madpacket.hpp"

#undef MAD_ASSERT

static inline volatile void* vptr(std::byte* p) noexcept {
  return reinterpret_cast<volatile void*>(p);
}
static inline volatile void const* vcptr(std::byte const* p) noexcept {
  return reinterpret_cast<volatile void const*>(p);
}

int main() {
  using namespace mad;

  using Bus = mad::reg::bus32;
  using P = packet<u32<"w0">>; // 4 bytes, simple scalar-only packet.

  static_assert(P::total_bytes == 4);

  // A small buffer we can create aligned/misaligned pointers into.
  alignas(16) std::array<std::byte, 64> buf{};
  auto* base = buf.data();

  // ---- Case 1: BaseAlign=16, correctly aligned pointer -> no assertion failures.
  test::reset();
  {
    auto v = mad::reg::make_view<P, Bus, 16>(vptr(base));
    (void)v;
    assert(test::assert_calls >= 1);
    assert(test::assert_failed == 0);
  }

  // ---- Case 2: BaseAlign=16, pointer aligned to 4 but not 16 -> assertion must fail.
  test::reset();
  {
    // offset 4 => 0b0100; breaks 16B alignment but keeps 4B alignment.
    auto* p = base + 4;
    auto v = mad::reg::make_view<P, Bus, 16>(vptr(p));
    (void)v;
    assert(test::assert_calls >= 1);
    assert(test::assert_failed >= 1);
  }

  // ---- Case 3: Default BaseAlign == Bus::align (4). Misaligned pointer -> assertion must fail.
  test::reset();
  {
    auto* p = base + 2; // not 4-aligned
    auto v = mad::reg::make_view<P, Bus>(vptr(p));
    (void)v;
    assert(test::assert_calls >= 1);
    assert(test::assert_failed >= 1);
  }

  // ---- Case 4: Default BaseAlign (4). Properly aligned pointer -> no assertion failures.
  test::reset();
  {
    auto* p = base + 4; // 4-aligned
    auto v = mad::reg::make_view<P, Bus>(vptr(p));
    (void)v;
    assert(test::assert_calls >= 1);
    assert(test::assert_failed == 0);
  }

  // ---- Case 5: BaseAlign=1 should accept any pointer (no failures).
  // This also verifies the condition is literally (addr & (BaseAlign-1)) == 0,
  // because BaseAlign-1 == 0 forces the check to always pass.
  test::reset();
  {
    auto* p = base + 3; // intentionally odd address
    auto v = mad::reg::make_view<P, Bus, 1>(vptr(p));
    (void)v;
    assert(test::assert_calls >= 1);
    assert(test::assert_failed == 0);
  }

  // ---- Case 6: const overload uses the same alignment assertion.
  test::reset();
  {
    auto* p = base + 2; // misaligned for 4
    auto v = mad::reg::make_view<P, Bus>(vcptr(p));
    (void)v;
    assert(test::assert_calls >= 1);
    assert(test::assert_failed >= 1);
  }

  return 0;
}
