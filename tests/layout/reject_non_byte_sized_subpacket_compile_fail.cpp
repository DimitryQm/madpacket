#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/layout/reject_non_byte_sized_subpacket_compile_fail.cpp  (compile-fail)
//
// Layout Model (§7) + Fields Reference (§6) + Semantics Contract (§8):
//
//   subpacket_field<SubPacket, Name> is byte-addressed by:
//       byte_offset = (bit_offset >> 3)
//   and returns a nested view over SubPacket::total_bytes bytes.
//
//   If SubPacket::total_bits is NOT a multiple of 8, SubPacket::total_bytes rounds up,
//   which means the nested view necessarily includes bits outside the declared subpacket
//   bit-range. Any nested bitfield RMW in that last byte can clobber parent-adjacent bits.
//   That is silent corruption.
//
//   The library now enforces:
//       static_assert(SubPacket::total_bits % 8 == 0)
//   at packet formation time (Packet::validate()).
//
// This file must FAIL TO COMPILE by merely forming a packet that contains a subpacket
// whose total_bits is not a whole number of bytes.
//
// Why this matters (concrete corruption story):
//   Suppose SubBad occupies 12 bits in the parent, followed immediately by a field 'post'.
//   SubBad::total_bytes == 2 (round-up), so the nested view covers bytes [sub..sub+2).
//   The last 4 bits of that second byte are NOT part of the subpacket but belong to 'post'.
//   If any nested write does a read-modify-write in that byte (bitfields do), it will
//   preserve/modify bits that are outside the subpacket slice and silently smash 'post'.
//
// The only safe rule is to require SubPacket::total_bits % 8 == 0.
//
using namespace mad;

// A subpacket whose total_bits is 12 (NOT divisible by 8).
using SubBad = packet<
  u8<"a">,    // 8 bits
  u4<"b">     // 4 bits
>;

static_assert(SubBad::total_bits == 12);
static_assert(SubBad::total_bits % 8 != 0);
static_assert(SubBad::total_bytes == 2); // rounds up -> the footgun

// This MUST fail at packet formation time due to whole-byte-size rule for subpackets.
using Parent = packet<
  u8<"pre">,
  subpacket<SubBad, "sub">,   // <-- should trigger packet::validate() static_assert
  u8<"post">
>;

// The rest is here only to make this translation unit look "normal" and to avoid
// toolchains that elide instantiations in weird ways. The compilation should have
// already failed above.
int main() {
  (void)sizeof(Parent);
  return 0;
}


/*
Other illustrative cases (kept in this file for auditability).

If you want to see additional failure modes, you can temporarily flip the guards
below. They are intentionally disabled so that this translation unit produces one
primary, stable diagnostic in most toolchains.

The important point: the error must arise during packet type formation (packet::validate),
not only when a field is accessed via get()/set().
*/

#if 0
// Another non-byte-sized subpacket: 9 bits total.
using SubBad2 = packet< u1<"x">, u8<"y"> >;
static_assert(SubBad2::total_bits == 9);
using Parent2 = packet< subpacket<SubBad2, "sub"> >;
#endif

#if 0
// Non-byte-sized subpacket embedded after a bitfield, to show it is independent of alignment.
// Even if the *start* is byte-aligned, the *size* is the hazard.
using SubBad3 = packet< u8<"a">, u1<"b"> >; // 9 bits
using Parent3 = packet< u8<"pre">, subpacket<SubBad3, "sub">, u8<"post"> >;
#endif


// Expected diagnostic (approximate)
// The exact wording depends on your compiler, but you should see something like:
//
//   static assertion failed: subpacket fields require SubPacket::total_bits % 8 == 0
//
// The critical property is *where* it fails:
//   - It must fail while instantiating `mad::packet<...>` for `Parent`.
//   - It must NOT require calling `get<"sub">()` or `set<...>()`.
//
// If this ever stops failing, the library has regressed back into "latent UB":
// you can form an illegal packet and only find out later (or never), which breaks trust.
