#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "madpacket.hpp"

// tests/contract/scope_and_terms.cpp
//
// Validates 01_semantics_contract.md §1 "Definitions and scope".
// This is primarily an API-surface and terminology lock-down test.
//
// It ensures the library exposes:
//  - mad::packet<Fields...> as a "packet layout"
//  - field descriptor categories (int/bits, bytes, pad, subpacket) via field_kind
//  - view families: mad::view / mad::cview, and mad::reg::view / mad::reg::cview,
//    plus policy views mad::reg::xview / mad::reg::xcview
//  - field names as fixed_string NTTPs usable for compile-time lookup
//
// This test is intentionally broad and mostly compile-time; detailed semantics are
// validated by focused tests elsewhere.

static inline unsigned u8(std::byte b) { return std::to_integer<unsigned>(b); }

namespace contract_scope {

using Sub = mad::packet<
  mad::u8<"sx">,
  mad::u8<"sy">
>;

using P = mad::packet<
  mad::u1<"flag">,
  mad::pad_bits<7>,                  // explicit pad to byte-align the subpacket
  mad::subpacket<Sub, "sub">,
  mad::bytes<"payload", 3>,
  mad::le_u16<"le16">,
  mad::be_u32<"be32">,
  mad::ibits<11, "s11">,
  mad::pad_bits<5>,
  mad::u8<"tail">
>;

static_assert(std::is_class_v<P>, "packet must be a type");
static_assert(P::total_bits > 0);
static_assert(P::total_bytes == ((P::total_bits + 7u) >> 3));

static_assert(P::field_count > 0);
static_assert(P::offsets_bits.size() == P::field_count);
static_assert(P::sizes_bits.size() == P::field_count);

// Validate fixed_string NTTP behavior.
template <mad::fixed_string Name>
struct name_echo {
  static constexpr auto value = Name;
};

static_assert(name_echo<"flag">::value.size() == 4);
static_assert(name_echo<"payload">::value.size() == 7);
static_assert(name_echo<"be32">::value.size() == 4);

// Validate name -> field type mapping exists (contract terminology: "field").
using F_flag    = mad::detail::field_by_name_t<P, "flag">;
using F_sub     = mad::detail::field_by_name_t<P, "sub">;
using F_payload = mad::detail::field_by_name_t<P, "payload">;
using F_le16    = mad::detail::field_by_name_t<P, "le16">;
using F_be32    = mad::detail::field_by_name_t<P, "be32">;
using F_s11     = mad::detail::field_by_name_t<P, "s11">;
using F_tail    = mad::detail::field_by_name_t<P, "tail">;

static_assert(F_flag::kind == mad::field_kind::int_bits);
static_assert(F_sub::kind == mad::field_kind::subpacket);
static_assert(F_payload::kind == mad::field_kind::bytes);
static_assert(F_le16::kind == mad::field_kind::int_bits);
static_assert(F_be32::kind == mad::field_kind::int_bits);
static_assert(F_s11::kind == mad::field_kind::int_bits);
static_assert(F_tail::kind == mad::field_kind::int_bits);

static_assert(F_payload::bytes == 3);
static_assert(std::is_same_v<typename F_sub::packet, Sub>);

static_assert(F_flag::bits == 1);
static_assert(F_le16::bits == 16);
static_assert(F_be32::bits == 32);
static_assert(F_s11::bits == 11);
static_assert(F_tail::bits == 8);

static_assert(std::is_same_v<typename F_le16::endian, mad::little_endian_t>);
static_assert(std::is_same_v<typename F_be32::endian, mad::big_endian_t>);

static_assert(F_s11::is_signed == true);
static_assert(F_le16::is_signed == false);

} // namespace contract_scope

int main() {
  using namespace contract_scope;

  // Ordinary views (byte buffers): mad::view / mad::cview
  std::array<std::byte, P::total_bytes> buf{};
  auto v  = mad::make_view<P>(buf.data(), buf.size());
  auto cv = mad::make_view<P>(static_cast<std::byte const*>(buf.data()), buf.size());

  static_assert(std::is_same_v<decltype(v),  mad::view<P>>);
  static_assert(std::is_same_v<decltype(cv), mad::cview<P>>);

  static_assert(std::is_same_v<decltype(v.base()),  std::byte*>);
  static_assert(std::is_same_v<decltype(cv.base()), std::byte const*>);

  // Name-based get types: integers widen to 64-bit types; subpacket/bytes return views.
  static_assert(std::is_same_v<decltype(v.get<"flag">()), std::uint64_t>);
  static_assert(std::is_same_v<decltype(v.get<"le16">()), std::uint64_t>);
  static_assert(std::is_same_v<decltype(v.get<"be32">()), std::uint64_t>);
  static_assert(std::is_same_v<decltype(v.get<"s11">()), std::int64_t>);
  static_assert(std::is_same_v<decltype(v.get<"tail">()), std::uint64_t>);

  auto sub_v = v.get<"sub">();
  auto sub_cv = cv.get<"sub">();
  (void)sub_cv;

  static_assert(std::is_same_v<decltype(sub_v),  mad::view<Sub>>);
  static_assert(std::is_same_v<decltype(sub_cv), mad::cview<Sub>>);

  auto payload_v = v.get<"payload">();
  static_assert(std::is_same_v<decltype(payload_v), mad::bytes_view<3>>);

  // Minimal behavioral sanity: set/get roundtrips and nested mutation compiles and runs.
  v.set<"flag">(1);
  v.set<"le16">(0xBEEFu);
  v.set<"be32">(0x11223344u);
  v.set<"s11">(-1);
  v.set<"tail">(0xAAu);

  sub_v.set<"sx">(0x12u);
  sub_v.set<"sy">(0x34u);

  auto sp = payload_v.as_span();
  sp[0] = std::byte{0xDE};
  sp[1] = std::byte{0xAD};
  sp[2] = std::byte{0xBE};

  assert(v.get<"flag">() == 1u);
  assert(v.get<"le16">() == 0xBEEFu);
  assert(v.get<"be32">() == 0x11223344u);
  assert(v.get<"s11">() == static_cast<std::int64_t>(-1));
  assert(v.get<"tail">() == 0xAAu);

  // The payload is byte-aligned by construction. Validate it mapped to the expected bytes.
  // Layout: byte0 = flag+pad, bytes1..2 = sub, bytes3..5 = payload.
  assert(u8(buf[3]) == 0xDEu);
  assert(u8(buf[4]) == 0xADu);
  assert(u8(buf[5]) == 0xBEu);

  // MMIO views exist: mad::reg::view / mad::reg::cview and xview/xcview
  // (Semantics are validated elsewhere; here we validate type presence/surface.)
  alignas(8) std::array<std::byte, P::total_bytes> regblk{};

  volatile std::byte* vbase = reinterpret_cast<volatile std::byte*>(regblk.data());
  volatile std::byte const* cvbase = reinterpret_cast<volatile std::byte const*>(regblk.data());

  mad::reg::view<P>  rv(vbase);
  mad::reg::cview<P> rcv(cvbase);

  (void)rv.get<"flag">();
  (void)rcv.get<"flag">();

  using Bus = mad::reg::bus64;
  using Cfg = mad::reg::cfg<Bus, 8, mad::reg::width_policy::native, mad::reg::align_policy::assert_>;

  auto xv  = mad::reg::make_xview<P, Cfg>(vbase);
  auto xcv = mad::reg::make_xcview<P, Cfg>(cvbase);

  static_assert(std::is_same_v<decltype(xv),  mad::reg::xview<P, Cfg>>);
  static_assert(std::is_same_v<decltype(xcv), mad::reg::xcview<P, Cfg>>);

  // A small safe operation to ensure the API links.
  xv.set<"tail">(0x55u);

  return 0;
}
