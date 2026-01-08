#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "madpacket.hpp"

// tests/api/index_access_mirrors_name_access.cpp
//
// Views (§3.3):
//   Index-based access get_i<I>() / set_i<I>(...) must route through the same
//   underlying access machinery as name-based get<Name>() / set<Name>(...).
//
// This runtime test validates:
//   - integer scalar + bitfield get/set equality between name and index paths
//   - bytes_field get via name/index returns identical region
//   - subpacket get via name/index returns identical nested base pointer and behavior
//   - indices count *all* fields including pads (pad itself is not accessed here)
//
// This is a "golden" mapping test: we check actual underlying bytes touched.

static inline unsigned byte_u(std::byte b) { return std::to_integer<unsigned>(b); }
static inline std::byte b8(unsigned v) { return std::byte{static_cast<unsigned char>(v & 0xFFu)}; }

int main() {
  using namespace mad;

  using Sub = packet<
    u8<"sx">,           // byte0
    ubits<3, "a">,      // byte1 bits0..2
    ubits<5, "b">,      // byte1 bits3..7
    u8<"sy">            // byte2
  >;
  static_assert(Sub::total_bits == 24);
  static_assert(Sub::total_bytes == 3);

  // Field index map (including pad):
  //   0 pre (u8)
  //   1 payload (bytes<3>)
  //   2 sub (subpacket<Sub>)
  //   3 flag (u1)
  //   4 pad_bits<7>   (not accessed)
  //   5 tail (le_u16)
  using P = packet<
    u8<"pre">,                 // byte0
    bytes<"payload", 3>,       // bytes1..3
    subpacket<Sub, "sub">,     // bytes4..6
    u1<"flag">,                // byte7 bit0
    pad_bits<7>,               // byte7 bits1..7
    le_u16<"tail">             // bytes8..9
  >;
  static_assert(P::total_bytes == 10);
  static_assert(P::field_count == 6);

  std::array<std::byte, P::total_bytes> buf{};
  // seed recognizable pattern
  for (std::size_t i = 0; i < buf.size(); ++i) buf[i] = b8(static_cast<unsigned>(0xA0u + i));

  auto v = make_view<P>(buf.data(), buf.size());

  // ---- pre (index 0) ----
  v.set<"pre">(0x11u);
  assert(static_cast<unsigned>(v.get<"pre">()) == 0x11u);
  assert(static_cast<unsigned>(v.get_i<0>()) == 0x11u);

  v.set_i<0>(0x22u);
  assert(static_cast<unsigned>(v.get<"pre">()) == 0x22u);
  assert(byte_u(buf[0]) == 0x22u);

  // ---- payload bytes (index 1) ----
  auto p_by_name  = v.get<"payload">();
  auto p_by_index = v.get_i<1>();

  assert(p_by_name.size() == 3);
  assert(p_by_index.size() == 3);
  assert(p_by_name.data() == buf.data() + 1);
  assert(p_by_index.data() == buf.data() + 1);

  auto sp1 = p_by_name.as_span();
  auto sp2 = p_by_index.as_span();
  assert(sp1.data() == sp2.data());
  assert(sp1.size() == sp2.size());

  sp1[0] = b8(0xDE);
  sp2[1] = b8(0xAD);
  sp1[2] = b8(0xBE);

  assert(byte_u(buf[1]) == 0xDEu);
  assert(byte_u(buf[2]) == 0xADu);
  assert(byte_u(buf[3]) == 0xBEu);

  // ---- subpacket (index 2) ----
  auto s_by_name  = v.get<"sub">();
  auto s_by_index = v.get_i<2>();

  assert(s_by_name.data() == buf.data() + 4);
  assert(s_by_index.data() == buf.data() + 4);
  assert(s_by_name.size_bytes() == Sub::total_bytes);
  assert(s_by_index.size_bytes() == Sub::total_bytes);

  // Write nested fields through name-get view.
  s_by_name.set<"sx">(0x12u);
  s_by_name.set<"a">(0b101u);
  s_by_name.set<"b">(0b11001u);
  s_by_name.set<"sy">(0x34u);

  // Sub bytes:
  //   byte4 = sx
  //   byte5 = (b<<3)|a
  //   byte6 = sy
  assert(byte_u(buf[4]) == 0x12u);
  assert(byte_u(buf[5]) == (((25u & 0x1Fu) << 3) | (5u & 0x7u)));
  assert(byte_u(buf[6]) == 0x34u);

  // Read back via index-get view to ensure same mapping.
  assert(static_cast<unsigned>(s_by_index.get<"sx">()) == 0x12u);
  assert(static_cast<unsigned>(s_by_index.get<"a">()) == 5u);
  assert(static_cast<unsigned>(s_by_index.get<"b">()) == 25u);
  assert(static_cast<unsigned>(s_by_index.get<"sy">()) == 0x34u);

  // Mutate via s_by_index and verify bytes update.
  s_by_index.set<"a">(0u);
  s_by_index.set<"b">(31u);
  assert(byte_u(buf[5]) == (((31u & 0x1Fu) << 3) | 0u));
  assert(static_cast<unsigned>(s_by_name.get<"a">()) == 0u);
  assert(static_cast<unsigned>(s_by_name.get<"b">()) == 31u);

  // ---- flag bitfield (index 3) ----
  v.set<"flag">(1u);
  assert(static_cast<unsigned>(v.get<"flag">()) == 1u);
  assert(static_cast<unsigned>(v.get_i<3>()) == 1u);
  assert((byte_u(buf[7]) & 0x01u) == 0x01u);

  v.set_i<3>(0u);
  assert(static_cast<unsigned>(v.get<"flag">()) == 0u);
  assert((byte_u(buf[7]) & 0x01u) == 0x00u);

  // ---- tail le_u16 (index 5) ----
  v.set<"tail">(0xBEEFu);
  assert(static_cast<unsigned>(v.get<"tail">()) == 0xBEEFu);
  assert(static_cast<unsigned>(v.get_i<5>()) == 0xBEEFu);
  assert(byte_u(buf[8]) == 0xEFu);
  assert(byte_u(buf[9]) == 0xBEu);

  v.set_i<5>(0x1234u);
  assert(static_cast<unsigned>(v.get<"tail">()) == 0x1234u);
  assert(byte_u(buf[8]) == 0x34u);
  assert(byte_u(buf[9]) == 0x12u);

  // Guard: earlier bytes unaffected by tail stores.
  assert(byte_u(buf[0]) == 0x22u);
  assert(byte_u(buf[1]) == 0xDEu);
  assert(byte_u(buf[4]) == 0x12u);

  return 0;
}
