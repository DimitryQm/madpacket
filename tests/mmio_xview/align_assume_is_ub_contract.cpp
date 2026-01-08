#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// tests/mmio_xview/align_assume_is_ub_contract.cpp
//
// Contract (§11):
//   If the alignment policy is align_policy::assume, the library provides an optimizer hint
//   that the pointer is aligned. If the pointer is NOT aligned, behavior is undefined (UB):
//     - the compiler may assume alignment and miscompile surrounding code
//     - the CPU may fault due to misaligned typed MMIO access
//
// Because UB cannot be reliably "tested" (a correct optimizer may do anything), this test
// validates what we *can* validate safely and usefully:
//
//   1) An aligned pointer works correctly under align_policy::assume (basic get/set sanity).
//   2) No MAD_ASSERT is consulted (assume does not perform checks).
//   3) The configuration type actually encodes align_policy::assume and base_align.
//
// Additionally, we include an opt-in UB demonstration block that can be enabled by defining
// MADPACKET_TEST_ENABLE_UB_DEMO. Do NOT enable it in normal CI; it's there to document
// the contract concretely for humans.

static inline std::uint64_t g_checks = 0;
static inline std::uint64_t g_fails  = 0;

#define MAD_ASSERT(x) do { ++g_checks; if(!(x)) ++g_fails; } while(0)

#include "madpacket.hpp"

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::align == 4);
  static_assert(Bus::bytes == 4);

  using P = mad::packet<
    mad::le_u32<"a32">,
    mad::ubits<7, "b7">,
    mad::pad_bits<1>,
    mad::u8<"tail">
  >;
  static_assert(P::total_bytes == 4 + 1 + 1);

  using CfgAssume8 = mad::reg::cfg<Bus, 8, mad::reg::width_policy::native, mad::reg::align_policy::assume>;

  static_assert(CfgAssume8::base_align == 8);
  static_assert(CfgAssume8::align == mad::reg::align_policy::assume);
  static_assert(std::is_same_v<typename CfgAssume8::bus_type, Bus>);

  alignas(8) std::array<std::byte, P::total_bytes + 8> storage{};
  for (auto& b : storage) b = std::byte{0xCC};

  volatile std::byte* base_aligned = reinterpret_cast<volatile std::byte*>(storage.data());
  volatile std::byte* base_misaligned = base_aligned + 1;

  // Aligned construction: should work and not consult MAD_ASSERT.
  g_checks = 0;
  g_fails  = 0;

  auto vx = mad::reg::make_xview<P, CfgAssume8>(reinterpret_cast<volatile void*>(base_aligned));
  vx.set<"a32">(0x11223344u);
  vx.set<"b7">(0x7Fu);
  vx.set<"tail">(0x5Au);

  assert(vx.get<"a32">() == 0x11223344u);
  assert(vx.get<"b7">() == 0x7Fu);
  assert(vx.get<"tail">() == 0x5Au);

  // Stored LE bytes must match tag regardless of host endian.
  assert(u8(storage[0]) == 0x44u);
  assert(u8(storage[1]) == 0x33u);
  assert(u8(storage[2]) == 0x22u);
  assert(u8(storage[3]) == 0x11u);

  // assume mode should not call MAD_ASSERT.
  assert(g_checks == 0);
  assert(g_fails  == 0);

  // UB demonstration (disabled by default):
  //
  // If you enable this, you're intentionally invoking UB and the result is not reliable.
  // It may:
  //   - appear to work
  //   - crash
  //   - produce corrupted reads/writes
  //   - be miscompiled (especially under -O2/-O3)
  //
  // This block exists to make the contract explicit and to support manual exploration.
#ifdef MADPACKET_TEST_ENABLE_UB_DEMO
  // WARNING: UB below!
  auto vx_bad = mad::reg::make_xview<P, CfgAssume8>(reinterpret_cast<volatile void*>(base_misaligned));
  // Any access is UB; keep it minimal.
  vx_bad.set<"tail">(0xAAu);
  (void)vx_bad.get<"tail">();
#endif

  // Even constructing vx_bad is UB only if the compiler assumes alignment during construction
  // and uses it for something observable. In this header, the assumption is applied in the
  // alignment enforcement helper. Some compilers may not manifest UB until you actually access.
  // Therefore: we do NOT touch vx_bad unless the demo is enabled.

  (void)base_misaligned;
  return 0;
}
