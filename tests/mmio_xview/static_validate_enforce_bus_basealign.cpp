#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// tests/mmio_xview/static_validate_enforce_bus_basealign.cpp
//
// Contract (§11):
//   static_validate<Packet, Cfg>() provides a compile-time validation hook.
//   In the current implementation, it enforces:
//     - If Cfg::width == width_policy::enforce_bus, then Cfg::base_align >= Bus::align.
//   This prevents obviously-invalid configurations from compiling.
//
// What this test validates:
//   1) For enforce_bus configs, layout_ok_v and static_validate accept BaseAlign >= Bus::align.
//   2) For enforce_bus configs, layout_ok_v becomes false when BaseAlign < Bus::align.
//      (We do NOT instantiate static_validate with the bad config because it would hard-fail compilation.)
//   3) For non-enforce-bus configs, BaseAlign is not required to be >= Bus::align.
//   4) xview types instantiate and function for a "good" enforce_bus config.
//
// This is primarily a compile-time test with a small runtime smoke at the end.

#include "madpacket.hpp"

namespace {
  using Bus32 = mad::reg::bus32;
  static_assert(Bus32::bytes == 4);
  static_assert(Bus32::bits == 32);
  static_assert(Bus32::align == 4);

  // A layout that includes both scalar and bitfield members.
  using P = mad::packet<
    mad::u32<"reg0">,            // 4 bytes
    mad::ubits<5, "bf0">,        // bitfield inside next bytes
    mad::pad_bits<3>,            // align to byte
    mad::u8<"tail">              // 1 byte
  >;
  static_assert(P::total_bytes == 6);

  // Enforce-bus config with adequate BaseAlign.
  using CfgGood = mad::reg::cfg_enforce_bus<Bus32, /*BaseAlign*/ Bus32::align>;

  // Enforce-bus config with insufficient BaseAlign (invalid).
  using CfgBad  = mad::reg::cfg<Bus32, /*BaseAlign*/ 2, mad::reg::width_policy::enforce_bus,
                                mad::reg::align_policy::unchecked, mad::reg::caps_bus_only<Bus32>>;

  // Non-enforce-bus config: BaseAlign can be smaller than Bus32::align (allowed by contract).
  using CfgNativeSmallAlign = mad::reg::cfg<Bus32, /*BaseAlign*/ 1, mad::reg::width_policy::native,
                                           mad::reg::align_policy::unchecked, mad::reg::caps_all>;

  // --------------------------
  // Compile-time validations
  // --------------------------
  static_assert(mad::reg::layout_ok_v<P, CfgGood>,
                "Expected layout_ok_v true when enforce_bus and BaseAlign >= Bus::align");

  static_assert(!mad::reg::layout_ok_v<P, CfgBad>,
                "Expected layout_ok_v false when enforce_bus and BaseAlign < Bus::align");

  static_assert(mad::reg::layout_ok_v<P, CfgNativeSmallAlign>,
                "Expected layout_ok_v true when NOT enforce_bus, regardless of BaseAlign vs Bus::align");

  // static_validate should compile for good config.
  constexpr bool kStaticValidateCompiles =
    (mad::reg::static_validate<P, CfgGood>(), true);
  static_assert(kStaticValidateCompiles);

  // xview_base calls static_validate internally; constructing a type should compile.
  using VXGood  = mad::reg::xview<P, CfgGood>;
  using VCXGood = mad::reg::xcview<P, CfgGood>;

  // Some introspection: for enforce_bus, transaction estimate is bus-word based.
  static_assert(mad::reg::layout_info<P, CfgGood>::template worst_case_transactions<0>() == 1,
                "u32 at offset 0 on bus32 is 1 transaction under enforce_bus");
  static_assert(mad::reg::layout_info<P, CfgGood>::template worst_case_transactions<3>() == 1,
                "u8 tail should still be 1 transaction estimate (bus-word based)");

  // For native policy, transaction estimate for u32 is also 1.
  static_assert(mad::reg::layout_info<P, CfgNativeSmallAlign>::template worst_case_transactions<0>() == 1);

  // Touch types to ensure ODR-use doesn't depend on LTO removing code.
  static_assert(sizeof(VXGood) >= sizeof(void*));
  static_assert(sizeof(VCXGood) >= sizeof(void*));
} // namespace

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

int main() {
  using Bus32 = mad::reg::bus32;
  using P = mad::packet<
    mad::u32<"reg0">,
    mad::ubits<5, "bf0">,
    mad::pad_bits<3>,
    mad::u8<"tail">
  >;
  using CfgGood = mad::reg::cfg_enforce_bus<Bus32, Bus32::align>;

  alignas(Bus32::align) std::array<std::byte, P::total_bytes> mem{};
  for (std::size_t i = 0; i < mem.size(); ++i) mem[i] = std::byte{static_cast<unsigned char>(0xA0u + i)};

  auto vx = mad::reg::make_xview<P, CfgGood>(reinterpret_cast<volatile void*>(mem.data()));
  auto vcx = mad::reg::make_xcview<P, CfgGood>(reinterpret_cast<volatile void const*>(mem.data()));
  (void)vcx;

  // Basic smoke: set/get preserve semantics.
  vx.set<"reg0">(0x11223344u);
  vx.set<"bf0">(0x1Fu);
  vx.set<"tail">(0x55u);

  assert(vx.get<"reg0">() == 0x11223344u);
  assert(vx.get<"bf0">() == 0x1Fu);
  assert(vx.get<"tail">() == 0x55u);

  // u32 is native-endian here; we only sanity-check that bytes changed, not exact order.
  const bool changed_some =
    (u8(mem[0]) != 0xA0u) || (u8(mem[1]) != 0xA1u) || (u8(mem[2]) != 0xA2u) || (u8(mem[3]) != 0xA3u);
  assert(changed_some);

  return 0;
}
