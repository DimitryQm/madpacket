#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// tests/bounds/direct_view_is_unchecked.cpp
//
// Contract (§9):
//   - Direct construction of mad::view/mad::cview from a pointer does not check buffer size.
//   - make_view(data, n) uses MAD_ASSERT to enforce n >= Packet::total_bytes.
//
// This file focuses on the "direct view is unchecked" property.
//
// We intercept MAD_ASSERT to prove:
//   (A) direct constructors do not call MAD_ASSERT
//   (B) make_view DOES call MAD_ASSERT (so our hook is live)
//   (C) both const and non-const paths behave as expected
//
// IMPORTANT:
//   We do NOT read/write fields through a view backed by an undersized buffer here.
//   That would be undefined behavior. We only test that construction performs no checks.

static inline int g_checks = 0;
static inline int g_fails  = 0;

#define MAD_ASSERT(x) do { ++g_checks; if(!(x)) ++g_fails; } while(0)
#include "madpacket.hpp"

int main() {
  using namespace mad;

  using P = packet<
    u8<"a">,
    u16<"b">,
    ubits<5, "c">,
    pad_bits<3>,
    u32<"d">,
    bytes<"blob", 8>,
    be_u16<"e">
  >;

  static_assert(P::total_bytes == 1 + 2 + 1 + 4 + 8 + 2);
  static_assert(std::is_trivially_copyable_v<view<P>>);
  static_assert(std::is_trivially_copyable_v<cview<P>>);

  // Part A: Direct construction must not consult MAD_ASSERT
  g_checks = 0;
  g_fails  = 0;

  // Tiny undersized buffer: construction must still not assert.
  std::array<std::byte, 1> tiny{std::byte{0}};

  view<P>  v(tiny.data());
  cview<P> cv(static_cast<std::byte const*>(tiny.data()));
  (void)v;
  (void)cv;

  if (g_checks != 0) return 1;
  if (g_fails  != 0) return 2;

  // Constructing from nullptr should also not consult MAD_ASSERT.
  // (Still unsafe to use; this is solely about "unchecked constructor".)
  g_checks = 0;
  g_fails  = 0;
  view<P>  vnull(static_cast<std::byte*>(nullptr));
  cview<P> cvnull(static_cast<std::byte const*>(nullptr));
  (void)vnull;
  (void)cvnull;

  if (g_checks != 0) return 3;
  if (g_fails  != 0) return 4;

  // Part B: make_view must consult MAD_ASSERT once (and fail for undersized buffer)
  g_checks = 0;
  g_fails  = 0;

  (void)make_view<P>(tiny.data(), tiny.size());
  if (g_checks != 1) return 5;
  if (g_fails  != 1) return 6;

  // Const overload too.
  g_checks = 0;
  g_fails  = 0;

  (void)make_view<P>(static_cast<std::byte const*>(tiny.data()), tiny.size());
  if (g_checks != 1) return 7;
  if (g_fails  != 1) return 8;

  // Part C: Direct construction with correct-size buffer still doesn't assert
  g_checks = 0;
  g_fails  = 0;

  std::array<std::byte, P::total_bytes> ok{};
  view<P>  v2(ok.data());
  cview<P> cv2(static_cast<std::byte const*>(ok.data()));
  (void)v2;
  (void)cv2;

  if (g_checks != 0) return 9;
  if (g_fails  != 0) return 10;

  // But make_view with correct size should assert-check once and pass.
  g_checks = 0;
  g_fails  = 0;

  auto v3 = make_view<P>(ok.data(), ok.size());
  if (g_checks != 1) return 11;
  if (g_fails  != 0) return 12;

  // Tiny safe use to ensure v3 is functional (only reads/writes within the valid buffer).
  v3.set<"a">(0x12u);
  v3.set<"b">(0x3456u);
  v3.set<"c">(0x1Fu);
  v3.set<"d">(0x11223344u);
  v3.set<"e">(0xBEEFu);

  if (v3.get<"a">() != 0x12u) return 13;
  if (v3.get<"b">() != 0x3456u) return 14;
  if (v3.get<"c">() != 0x1Fu) return 15;
  if (v3.get<"d">() != 0x11223344u) return 16;
  if (v3.get<"e">() != 0xBEEFu) return 17;

  return 0;
}
