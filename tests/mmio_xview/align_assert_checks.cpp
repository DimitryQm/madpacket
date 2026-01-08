#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// tests/mmio_xview/align_assert_checks.cpp
//
// Contract (§11):
//   If the alignment policy is align_policy::assert_
//     - the library checks alignment using MAD_ASSERT
//     - on failure, behavior is that of MAD_ASSERT (default assert-fail; here we instrument)
//     - it also asserts that the alignment value (base_align or Bus::align enforcement) is a power of two.
//
// This test instruments MAD_ASSERT and verifies:
//   - make_xview triggers MAD_ASSERT checks in assert_ mode
//   - misaligned pointers cause an assertion failure (g_fails increases)
//   - non-power-of-two base_align triggers the "is_pow2" assertion failure
//   - aligned pointers pass (no failures)
//
// IMPORTANT:
//   We use an instrumented MAD_ASSERT that does NOT abort so we can observe counts.

static inline std::uint64_t g_checks = 0;
static inline std::uint64_t g_fails  = 0;

#define MAD_ASSERT(x) do { ++g_checks; if(!(x)) ++g_fails; } while(0)

#include "madpacket.hpp"

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::align == 4);

  using P = mad::packet<
    mad::u32<"w0">,
    mad::u8<"tail">
  >;
  static_assert(P::total_bytes == 5);

  // Config: enforce base alignment 8 with assert_ policy.
  using CfgAssert8 = mad::reg::cfg<Bus, 8, mad::reg::width_policy::native, mad::reg::align_policy::assert_>;

  alignas(8) std::array<std::byte, P::total_bytes + 8> storage{};

  volatile std::byte* base_aligned = reinterpret_cast<volatile std::byte*>(storage.data());
  volatile std::byte* base_misaligned = base_aligned + 1;

  // Aligned: expect checks performed, but no failures.
  // enforce_alignment(assert_) executes:
  //   MAD_ASSERT(is_pow2(a));
  //   MAD_ASSERT(is_aligned(p, a));
  // so we expect 2 checks, 0 fails for each call to make_xview.
  g_checks = 0;
  g_fails  = 0;

  auto vx_ok  = mad::reg::make_xview<P, CfgAssert8>(reinterpret_cast<volatile void*>(base_aligned));
  auto vcx_ok = mad::reg::make_xcview<P, CfgAssert8>(reinterpret_cast<volatile void const*>(base_aligned));
  (void)vx_ok;
  (void)vcx_ok;

  // Two checks for each make_* call: pow2 + alignment.
  assert(g_checks == 4);
  assert(g_fails  == 0);

  // Misaligned: expect checks performed and at least one failure (alignment check).
  // The pow2 check should pass; the is_aligned check should fail.
  g_checks = 0;
  g_fails  = 0;

  auto vx_bad  = mad::reg::make_xview<P, CfgAssert8>(reinterpret_cast<volatile void*>(base_misaligned));
  auto vcx_bad = mad::reg::make_xcview<P, CfgAssert8>(reinterpret_cast<volatile void const*>(base_misaligned));
  (void)vx_bad;
  (void)vcx_bad;

  assert(g_checks == 4);
  assert(g_fails  >= 2); // one per call, from is_aligned failure

  // Non-power-of-two base_align: should trip the is_pow2 assertion.
  // We cannot rely on any specific pointer alignment mod 3, but is_pow2(3) is false,
  // so at least one assertion failure must occur per make_* call.
  using CfgAssert3 = mad::reg::cfg<Bus, 3, mad::reg::width_policy::native, mad::reg::align_policy::assert_>;

  g_checks = 0;
  g_fails  = 0;

  auto vx_weird  = mad::reg::make_xview<P, CfgAssert3>(reinterpret_cast<volatile void*>(base_aligned));
  auto vcx_weird = mad::reg::make_xcview<P, CfgAssert3>(reinterpret_cast<volatile void const*>(base_aligned));
  (void)vx_weird;
  (void)vcx_weird;

  // Still 2 checks per call; pow2 must fail so failures >= 2.
  assert(g_checks == 4);
  assert(g_fails  >= 2);

  // Extra: width_policy::enforce_bus triggers an additional alignment enforcement
  // for Bus::align (contract mentions this for make_xview).
  // For Bus::align==4, our base_aligned (8-aligned) satisfies it.
  // We only check that it adds checks, not the exact count (implementation detail).
  using CfgAssertEnforceBus = mad::reg::cfg<Bus, 8, mad::reg::width_policy::enforce_bus, mad::reg::align_policy::assert_>;

  g_checks = 0;
  g_fails  = 0;

  auto vx_ok2 = mad::reg::make_xview<P, CfgAssertEnforceBus>(reinterpret_cast<volatile void*>(base_aligned));
  (void)vx_ok2;

  // At least pow2+align for base_align plus pow2+align for Bus::align => >= 4 checks.
  assert(g_checks >= 4);
  assert(g_fails == 0);

  return 0;
}
