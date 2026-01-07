# Semantics Contract

This contract is written against the behavior implemented in `madpacket.hpp` as provided. Where the implementation’s behavior is strict but not explicitly enforced by `static_assert` or runtime checks, this document still treats it as a required precondition, because relying on the current “accidental behavior” would produce silent corruption instead of a hard failure.


## 1. Definitions and scope


A “packet layout” is the compile-time type `mad::packet<Fields...>`.


A “field” is one of the field descriptor types used inside `mad::packet`, specifically `mad::int_field`, `mad::bytes_field`, `mad::pad_bits` / `mad::pad_bytes`, or `mad::subpacket_field`.


A “view” is a lightweight object that binds a packet layout to a raw buffer address without copying. In this library there are three view families: `mad::view` / `mad::cview` for ordinary byte buffers, `mad::reg::view` / `mad::reg::cview` for volatile MMIO access, and `mad::reg::xview` / `mad::reg::xcview` for volatile MMIO access with explicit width and alignment policy.


A “buffer byte index” is the byte offset from the view’s base pointer. Byte index 0 is the first byte of the packet.


A “buffer bit index” is the bit offset from the view’s base pointer under the library’s bit numbering model defined below. Bit index 0 is a specific bit inside byte index 0 (defined precisely in the bit numbering section).


A “bit offset” for a field is the compile-time sum of the `bits` of all preceding fields in the `mad::packet<...>` parameter pack. The first field’s bit offset is 0. `mad::packet` does not insert any implicit padding; any padding is explicit via `pad_bits` / `pad_bytes`.


An “integer field” is a field with `field_kind::int_bits`. Integer fields have a `bits` width in 1..64 and a `Signed` flag.


A “byte-aligned scalar integer field” in the sense of the implementation is an integer field whose bit offset is a multiple of 8 and whose width is exactly 8, 16, 32, or 64 bits. These fields use a direct byte-copy path plus optional byte swap based on the field endianness tag.


A “bitfield integer field” in the sense of the implementation is any integer field that is not a byte-aligned scalar integer field. This includes non-byte-aligned integer fields of any width, and also includes byte-aligned integer fields whose width is not exactly 8/16/32/64 (for example 24-bit). Bitfield integer fields are read and written using the library’s bit numbering model and do not support an explicit endianness tag other than native.


This contract defines observable semantics: which bits/bytes are read or written, how endianness is interpreted, what happens on out-of-range writes, and what ordering/width/alignment rules apply to volatile MMIO views.


Validated by tests/contract/scope_and_terms.cpp.


## 2. Layout packing and offsets


Field packing is contiguous in the declared order. For `using P = mad::packet<F0, F1, F2, ...>`, the bit offset of `Fi` is the sum of `Fj::bits` for all `j < i`. There is no implicit alignment, no implicit byte padding, and no implicit “struct-like” padding.


`P::total_bits` is the sum of all field bit widths. `P::total_bytes` is the ceiling of `P::total_bits / 8`.


`pad_bits<N>` occupies exactly N bits and is not addressable. `pad_bytes<N>` is exactly `pad_bits<N*8>`.


Because packing is bit-precise, fields can begin and end at arbitrary bit offsets, not just on byte boundaries.


Validated by tests/layout/packing_offsets_golden.cpp.


## 3. Bit numbering and bitfield packing model


This library uses a specific bit numbering model for all bitfield operations (and for any operation that conceptually manipulates a bit window). That model is independent of the host CPU endianness.


The model is “little-endian byte-stream numeric” for bit addressing:


Byte index 0 is the lowest-addressed byte in the buffer. Within a byte, bit index 0 is the least significant bit of that byte, and bit index 7 is the most significant bit of that byte. Bit indices increase first within the byte (from LSB to MSB), then across bytes in increasing address order.


Equivalently, if you interpret the buffer as an infinite little-endian integer where byte 0 contributes bits 0..7, byte 1 contributes bits 8..15, and so on, then a field at bit offset `k` and width `w` corresponds to bits `[k, k+w)` of that little-endian integer.


A concrete diagram for the first two bytes:


byte[0] bits: 7 6 5 4 3 2 1 0
^ ^
| |
bit 7 bit 0byte[1] bits: 15 14 13 12 11 10 9 8
^ ^
| |
bit 15 bit 8

When a bitfield spans bytes, the lower-addressed byte contains the lower-numbered bits of the field, and the higher-addressed byte contains the higher-numbered bits of the field.


All bitfield reads and writes in non-volatile `mad::view` / `mad::cview` follow this model.


All bitfield reads and writes in `mad::reg::view` / `mad::reg::cview` and `mad::reg::xview` / `mad::reg::xcview` also follow this model, including the “fits in one bus word” fast paths (which explicitly load a bus word as a little-endian byte-stream numeric before extracting bits).


Validated by tests/bit_order/bit0_is_lsb_byte0_golden.cpp and tests/bit_order/cross_byte_bitfield_golden.cpp and tests/mmio/bitfield_bus_word_le_stream.cpp.


## 4. Integer field value semantics (get)


For `mad::view` / `mad::cview`, `get<Name>()` on an integer field returns an integer value widened to 64 bits.


If the field is unsigned (`Signed == false`), `get<Name>()` returns a `std::uint64_t` value whose low `bits` bits equal the field’s stored value, with all higher bits zero.


If the field is signed (`Signed == true`), `get<Name>()` returns a `std::int64_t` value equal to the two’s complement interpretation of the field’s stored `bits` bits, sign-extended to 64 bits.


For byte-aligned scalar integer fields (offset multiple of 8 and width in {8,16,32,64}), sign extension is performed by converting the loaded unsigned integer of that width to the corresponding signed integer of the same width, then widening to `std::int64_t`.


For bitfield integer fields, sign extension is performed directly from the `bits` width using two’s complement sign extension.


Validated by tests/int/get_unsigned_zero_extend.cpp and tests/int/get_signed_sign_extend.cpp and tests/int/get_signed_cross_byte_bitfield.cpp.


## 5. Integer field write semantics (set) and out-of-range behavior


`set<Name>(v)` on an integer field writes the low `bits` bits of the input value into the field and discards all higher bits. There is no range checking and no assertion of “value fits in field width”.


This is not an optional optimization detail; it is the semantic contract of the current implementation.


For byte-aligned scalar integer fields (width 8/16/32/64 at byte-aligned offset), the input value is converted to an unsigned integer type of the corresponding width by `static_cast`, which truncates modulo 2^bits. This means that setting an 8-bit field with 0x1FF stores 0xFF, setting a 16-bit field with 0x1'0000 stores 0x0000, and so on.


For bitfield integer fields, the input is explicitly masked with `(1<<bits)-1` (or all ones for bits==64), which also truncates modulo 2^bits.


For signed fields, the input is first converted to an unsigned 64-bit value using the language’s integral conversion rules and then truncated to `bits`. Practically, this means negative values are stored as their two’s complement representation truncated to the field width. There is no assertion that the signed value is in the nominal signed range for that width.


Validated by tests/int/set_truncates_mod_2powbits.cpp and tests/int/set_signed_negative_two_complement.cpp and tests/int/set_bitfield_masks.cpp.


## 6. Endianness tags and their meaning


Endianness tags exist only on `int_field` and only influence byte-aligned scalar integer fields of width exactly 8, 16, 32, or 64 bits.


The endianness tags are `mad::native_endian`, `mad::little_endian`, and `mad::big_endian`, represented internally by `native_endian_t`, `little_endian_t`, and `big_endian_t`.


For byte-aligned scalar integer fields, the tag defines the byte order of the field in memory, relative to the numeric value returned by `get` and accepted by `set`.


If the tag is native endian, the field’s bytes are stored in the host CPU’s native endianness. This is intentionally host-dependent and is not a portable on-the-wire format unless the host endianness is fixed by deployment assumptions.


If the tag is little endian, the field’s bytes are stored least-significant byte first in memory, independent of the host CPU endianness.


If the tag is big endian, the field’s bytes are stored most-significant byte first in memory, independent of the host CPU endianness.


For 8-bit fields, endianness is semantically irrelevant because the byte order of a single byte is trivial. The implementation still permits a tag but it does not change the stored byte.


For any integer field that is not a byte-aligned scalar integer field in the implementation’s sense, the field must have native endianness. A non-native endianness tag on such a field is ill-formed and is rejected at compile time by `static_assert` in the access path.


This includes any non-byte-aligned integer field of any width, and includes byte-aligned widths that are not exactly 8/16/32/64. In particular, a 24-bit integer field is treated as a bitfield by the access implementation and therefore cannot legally specify little/big endian.


Validated by tests/endian/le_be_roundtrip_scalar.cpp and tests/endian/native_is_host_dependent.cpp and tests/endian/reject_non_scalar_endian_tag_compile_fail.cpp.


## 7. Read-modify-write boundaries for non-volatile views


For `mad::view` / `mad::cview` on ordinary memory, integer field reads and writes have the following byte-touch semantics.


For byte-aligned scalar integer fields, `get` reads exactly the field’s byte width beginning at the field’s byte offset, and `set` writes exactly the field’s byte width beginning at the field’s byte offset. There is no attempt to preserve adjacent bytes because the field occupies full bytes.


For bitfield integer fields, `get` reads the minimal byte window that contains the field’s bit range (from the field’s starting byte through the last byte touched by the field’s end bit). `set` performs a read-modify-write of that same byte window: it preserves all bits outside the field within the touched bytes, and updates only the field’s bits.


This preservation property is limited to the bytes touched by the field window. If two distinct fields overlap (which should not occur in a sane layout, but can be constructed if you embed subpackets or byte fields incorrectly), the last write wins for overlapping bits.


Validated by tests/bitfields/rmw_preserves_neighbor_bits.cpp and tests/bitfields/window_minimality.cpp.


## 8. Bytes fields and subpacket fields: byte alignment requirement


`bytes_field<Name, N>` and `subpacket_field<Packet, Name>` are addressed using the field’s byte offset computed as `bit_offset >> 3` and do not incorporate the bit-in-byte shift.


Therefore, the contract requires that any bytes field and any subpacket field begin on a byte boundary. Concretely, the field’s bit offset must be a multiple of 8.


If a bytes field or subpacket field is not byte-aligned, the view returned by `get` will point to `floor(bit_offset/8)` and will silently ignore the intra-byte shift. This is not a meaningful interpretation of a non-byte-aligned bytes region or nested packet, and it is treated by this contract as undefined behavior at the library level. The library does not currently reject this construction at compile time.


Validated by tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp.


## 9. Bounds and safety checks: what is asserted vs what is guaranteed


This library’s “safe construction helpers” (`mad::make_view`, `mad::file::make_view`, and the MMIO view constructors) use `MAD_ASSERT` to enforce preconditions such as buffer length and address alignment.


By default, `MAD_ASSERT(x)` is `assert(x)`. If assertions are compiled out (for example by defining `NDEBUG` in the standard library’s assert model, or by overriding `MAD_ASSERT`), then these checks do not execute.


The contract is therefore:


If you use a make_view helper, you must satisfy its documented preconditions even in release builds. If you do not, behavior is undefined, and the library may read or write out of bounds or perform misaligned MMIO transactions.


The ordinary non-file `mad::view` / `mad::cview` types can be constructed directly from a pointer without any size argument. In that case there is no check of buffer size at all; the caller is fully responsible for providing a valid backing buffer of at least `Packet::total_bytes`.


Validated by tests/bounds/make_view_asserts.cpp and tests/bounds/direct_view_is_unchecked.cpp.


## 10. Volatile MMIO semantics: `mad::reg::view` and `mad::reg::cview`


`mad::reg::view<Packet, Bus, BaseAlign>` and `mad::reg::cview<...>` provide the same named-field get/set interface as non-volatile views, but operate on a volatile base pointer (`volatile std::byte*` or `volatile std::byte const*`).


The observable intent is that reads and writes are performed using volatile operations, not cached copies, and therefore reflect MMIO register semantics.


For byte-aligned scalar integer fields, `get` uses a volatile load of width equal to the field width when possible. “When possible” means both of the following are true: `MADPACKET_STRICT_MMIO` is not defined, and the field address is aligned for the scalar type used (8/16/32/64). If these conditions are not met, the implementation falls back to bytewise volatile loads and assembles the value in host order before applying any declared endianness conversion.


For byte-aligned scalar integer fields, `set` uses `MAD_MMIO_BARRIER()` immediately before and immediately after the volatile store. If `MAD_MMIO_BARRIER` is not defined, it defaults to a no-op. There is no barrier automatically inserted around `get`.


For bitfield integer fields, the MMIO implementation uses two modes.


If the bitfield is fully contained within a single `Bus::word` region as computed by the bit offset and `Bus::bits`, the implementation reads that bus word as a little-endian byte-stream numeric, modifies the bit window, and writes the bus word back. This is a logical bus-word RMW.


This logical bus-word RMW is not a guarantee of a single hardware bus transaction. In the current implementation of `mad::reg::view`, the bus word is assembled and stored using per-byte volatile operations (`vload_u8` / `vstore_u8`) rather than necessarily using a typed `volatile Bus::word` access. If you require enforced hardware transaction widths, you must use `mad::reg::xview` with an appropriate configuration and you must avoid configurations that allow fallback to bytewise access on your target.


For bitfield integer fields that are not fully contained in one bus word, the implementation performs a byte-window read-modify-write: it reads each byte touched by the bitfield window with volatile byte reads into a temporary array, performs the bit update in that temporary array using the non-volatile bitfield algorithm, then writes each byte back with volatile byte writes. This preserves bits outside the field within the touched bytes, but it can produce multiple volatile byte transactions and can span multiple bus words.


For bitfield `set`, `MAD_MMIO_BARRIER()` is placed immediately before and immediately after the store sequence, but not around the initial reads.


Validated by tests/mmio/basic_scalar_endian.cpp and tests/mmio/barrier_placement.cpp and tests/mmio/bitfield_one_word_rmw.cpp and tests/mmio/bitfield_fallback_byte_window.cpp.


## 11. Policy MMIO semantics: configuration types and alignment policies


`mad::reg::xview` and `mad::reg::xcview` add explicit compile-time configuration via `mad::reg::cfg<...>`.


A configuration type `Cfg` defines the bus word type, a base alignment requirement, a width selection policy, an alignment enforcement policy, and read/write capability masks for allowed transaction widths.


The alignment policy values have the following contract-level meaning.


If the policy is `align_policy::unchecked`, the library performs no runtime check and provides no optimizer hint. If the address is misaligned for the transactions that occur, the behavior is whatever the target does for misaligned MMIO access, including potential faults. This policy exists for callers that already prove alignment externally and want zero overhead.


If the policy is `align_policy::assert_`, the library checks alignment using `MAD_ASSERT`. On failure, the behavior is that of `MAD_ASSERT` (by default, assertion failure). If assertions are compiled out or overridden, the check does not execute and the result is effectively unchecked. The assertion policy also asserts that the alignment value is a power of two.


If the policy is `align_policy::trap`, the library checks alignment at runtime and calls a trap instruction on failure (`__builtin_trap` on GCC/Clang, `__fastfail` on MSVC, or a forced fault fallback). This does not depend on `MAD_ASSERT` and is intended for targets where misalignment is always a hard fault and you want an unconditional fail-fast behavior.


If the policy is `align_policy::assume`, the library provides an optimizer assumption that the pointer is aligned. If the pointer is not aligned, behavior is undefined (the compiler may miscompile surrounding code because it is allowed to assume the alignment precondition holds). This policy is only correct when alignment is proven by construction.


`make_xview` and `make_block_view` enforce the configuration’s base alignment using the configured alignment policy. Additionally, if the width policy is `width_policy::enforce_bus`, `make_xview` enforces `Bus::align` as well.


`static_validate<Packet, Cfg>()` provides a compile-time validation hook. In the current implementation it enforces one required rule: if `Cfg::width` is `enforce_bus`, then `Cfg::base_align` must be at least `Bus::align`. This prevents obviously-invalid configurations from compiling.


Validated by tests/mmio_xview/align_unchecked_no_check.cpp and tests/mmio_xview/align_assert_checks.cpp and tests/mmio_xview/align_trap_traps.cpp and tests/mmio_xview/align_assume_is_ub_contract.cpp and tests/mmio_xview/static_validate_enforce_bus_basealign.cpp.


## 12. Policy MMIO semantics: width policies, masks, and actual access behavior


The width policy enum exists to express intent about transaction sizing. In this implementation, the strongest and most reliable notion of “bus transaction width” comes from the configured `Bus::word` type combined with whether typed volatile accesses are permitted.


The capability masks (`Cfg::read_mask` and `Cfg::write_mask`) are represented as a 4-bit mask over byte widths {1,2,4,8}. They participate in compile-time width selection decisions. In the current implementation, they do not hard-fail the operation if no width can be selected; instead, the code falls back to bus-word based algorithms. This contract therefore treats the masks as selection hints rather than a strict enforcement mechanism unless you additionally use build-time constraints such as `MADPACKET_MMIO_HARDWIDTH` and avoid strict-MMIO fallback.


For byte-aligned scalar integer fields in `xview`:


If `Cfg::width` is `width_policy::native` and the exact field width is permitted by `Cfg::read_mask` (for reads) or `Cfg::write_mask` (for writes), then the implementation uses the base MMIO scalar load/store path (`mmio_load_pod` / `mmio_store_pod`) which may perform a typed volatile access of exactly that width when alignment permits and `MADPACKET_STRICT_MMIO` is not defined.


Otherwise, the implementation performs bus-word based access to assemble or store the field bytes. Concretely, reads gather the field’s bytes by reading one or more bus words (typed volatile `Bus::word` loads when permitted, otherwise bytewise fallback) and extracting the relevant bytes in memory order. Writes store the field’s bytes by writing one or more bus words, preserving unrelated bytes via read-modify-write when the field does not cover entire bus words.


Endianness tags (little/big/native) apply to these byte-aligned scalar integer fields in xview exactly as in non-volatile views: the stored memory byte order is defined by the tag, independent of whether the access is performed via bus words or exact-width loads/stores.


For bitfield integer fields in `xview`:


If the bitfield is fully contained within a single bus word, the implementation performs a bus-word read, modifies the bit window in a little-endian byte-stream numeric representation of that bus word, and performs a bus-word write back. This yields one logical bus-word load and one logical bus-word store. Whether those are single hardware transactions depends on whether typed volatile bus-word access is available on the target and not forbidden by `MADPACKET_STRICT_MMIO`.


If the bitfield is not contained within a single bus word, the implementation falls back to a byte-window algorithm identical in structure to the basic `reg::view` fallback: volatile byte reads into a temporary window, bit update, volatile byte writes.


For xview stores, `MAD_MMIO_BARRIER()` is placed immediately before and immediately after the store sequence. For xview loads, there is no barrier automatically inserted.


Validated by tests/mmio_xview/native_exact_uses_scalar_width.cpp and tests/mmio_xview/non_native_path_uses_bus_words.cpp and tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp and tests/mmio_xview/bitfield_fallback_byte_window.cpp and tests/mmio_xview/barrier_placement.cpp.


## 13. Strict MMIO mode and hard width enforcement macros


If `MADPACKET_STRICT_MMIO` is defined, the implementation disables certain typed volatile load/store fast paths and may fall back to bytewise volatile operations even when the configuration expresses a desire for bus-word transactions.


This can be necessary on some targets to avoid problematic typed volatile aliasing, but it can violate hardware rules on targets that forbid narrow transactions. The library cannot universally detect such hardware constraints.


If `MADPACKET_MMIO_HARDWIDTH` is defined, certain bus-word access helpers in the xview implementation will reject compilation when they would otherwise be forced to perform bytewise fallback under strict-MMIO restrictions. Specifically, the contract is that enabling hardwidth turns “fall back to bytewise under strict mode” into a compile-time failure for bus-word operations that require typed access.


Because these macros affect whether the library can guarantee particular transaction widths, the contract considers them part of the MMIO semantic surface.


Validated by tests/mmio_xview/strict_mmio_fallback_behavior.cpp and tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


## 14. Non-addressable fields and illegal operations


Pad fields are not addressable. Attempting to `get` or `set` a pad field is ill-formed and is rejected at compile time.


Bytes fields do not support `set<Name>(...)`. A bytes field must be mutated by obtaining the bytes reference from `get<Name>()` and writing into it (for non-volatile views) or performing explicit volatile stores (for MMIO views). Attempting to call `set` on a bytes field is ill-formed and rejected at compile time.


Subpacket fields do not support assignment via `set<Name>(...)`. You obtain the nested view via `get<Name>()` and then operate on that nested view. Attempting to call `set` on a subpacket field is ill-formed and rejected at compile time.


Attempting to call `set` on a const view type (`mad::cview`, `mad::reg::cview`, `mad::reg::xcview`) is ill-formed and rejected at compile time.


Validated by tests/api/reject_set_on_const_view_compile_fail.cpp and tests/api/reject_get_set_on_pad_compile_fail.cpp and tests/api/reject_set_on_bytes_compile_fail.cpp and tests/api/reject_set_on_subpacket_compile_fail.cpp.


## 15. Name lookup and uniqueness


Field names are compile-time `fixed_string` NTTPs. In a single `mad::packet<...>`, all named fields must have unique names. Duplicate field names are ill-formed and rejected at compile time.


`get<Name>()` and `set<Name>(...)` are compile-time name lookups. If `Name` does not exist in the packet, the program is ill-formed and is rejected at compile time.


Validated by tests/names/unique_names_required_compile_fail.cpp and tests/names/name_not_found_compile_fail.cpp.



