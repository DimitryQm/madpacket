#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// tests/mmio_xview/align_unchecked_no_check.cpp
//
// Contract (§11):
//   If the alignment policy is align_policy::unchecked, the library performs
//   no runtime check and provides no optimizer hint. Misalignment is the caller's
//   responsibility and may fault / be undefined depending on platform + access widths.
//
// This test validates the *observable* part we can test safely:
//   - make_xview/make_xcview do NOT call MAD_ASSERT under align_policy::unchecked,
//     even when given a pointer that violates base_align.
//   - In contrast, we also construct a known-aligned view and perform a tiny safe
//     read/write to ensure the view is otherwise functional.
//
// IMPORTANT SAFETY NOTE:
//   We DO NOT access fields through a misaligned unchecked view; doing so could be UB
//   (typed volatile loads/stores on a misaligned address). We only test construction.

static inline std::uint64_t g_checks = 0;
static inline std::uint64_t g_fails  = 0;

// Instrument MAD_ASSERT to observe whether alignment checking happens.
#define MAD_ASSERT(x) do { ++g_checks; if(!(x)) ++g_fails; } while(0)

#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::bytes == 4);
  static_assert(Bus::align == 4);

  // A small packet with both scalar and bitfield elements.
  using P = mad::packet<
    mad::le_u32<"a32">,
    mad::ubits<5, "b5">,
    mad::pad_bits<3>,
    mad::u8<"tail">
  >;
  static_assert(P::total_bytes == 4 + 1 + 1);

  using CfgUnchecked = mad::reg::cfg<Bus, 8, mad::reg::width_policy::native, mad::reg::align_policy::unchecked>;

  // Allocate storage with known alignment for the base pointer.
  alignas(8) std::array<std::byte, P::total_bytes + 8> storage{};
  for (auto& b : storage) b = std::byte{0};

  // Create an intentionally MISALIGNED pointer by offsetting by 1.
  volatile std::byte* base_aligned = reinterpret_cast<volatile std::byte*>(storage.data());
  volatile std::byte* base_misaligned = base_aligned + 1;

  // Reset assertion counters; unchecked policy must not consult MAD_ASSERT at all.
  g_checks = 0;
  g_fails  = 0;

  // Construction should NOT assert-check anything.
  auto vx_bad  = mad::reg::make_xview<P, CfgUnchecked>(reinterpret_cast<volatile void*>(base_misaligned));
  auto vcx_bad = mad::reg::make_xcview<P, CfgUnchecked>(reinterpret_cast<volatile void const*>(base_misaligned));
  (void)vx_bad;
  (void)vcx_bad;

  assert(g_checks == 0);
  assert(g_fails  == 0);

  // Also test that constructing with a non-power-of-two base_align does NOT assert in unchecked mode.
  // (This is intentional: unchecked does no validation. The contract for assert_ specifically
  // asserts power-of-two; unchecked does not.)
  using CfgUncheckedWeird = mad::reg::cfg<Bus, 3, mad::reg::width_policy::native, mad::reg::align_policy::unchecked>;

  g_checks = 0;
  g_fails  = 0;
  auto vx_weird = mad::reg::make_xview<P, CfgUncheckedWeird>(reinterpret_cast<volatile void*>(base_misaligned));
  (void)vx_weird;
  assert(g_checks == 0);
  assert(g_fails  == 0);

  // Now validate normal usage with a properly aligned pointer is functional.
  g_checks = 0;
  g_fails  = 0;

  auto vx_ok = mad::reg::make_xview<P, CfgUnchecked>(reinterpret_cast<volatile void*>(base_aligned));
  vx_ok.set<"a32">(0x11223344u);
  vx_ok.set<"b5">(0x1Fu);
  vx_ok.set<"tail">(0xAAu);

  assert(vx_ok.get<"a32">() == 0x11223344u);
  assert(vx_ok.get<"b5">() == 0x1Fu);
  assert(vx_ok.get<"tail">() == 0xAAu);

  // Stored bytes for le_u32 are little endian by tag regardless of host endianness.
  assert(u8(storage[0]) == 0x44u);
  assert(u8(storage[1]) == 0x33u);
  assert(u8(storage[2]) == 0x22u);
  assert(u8(storage[3]) == 0x11u);

  // In unchecked mode there should still be no assertions invoked for aligned usage.
  assert(g_checks == 0);
  assert(g_fails  == 0);

  return 0;
}
