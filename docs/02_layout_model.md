# Layout Model

This document defines how madpacket computes field offsets and sizes, what “total size” means, and what layout invariants must hold for the implementation to be correct.

This document does not restate bit numbering, endianness, truncation semantics, or volatile access ordering. Those are specified in docs/01_semantics_contract.md.

## 1. What a layout is

A layout is the compile-time result of instantiating `mad::packet<Fields...>`.

The only “layout inputs” are the field descriptor types in the template parameter pack. There is no runtime schema, no reflection-based discovery, and no dynamic size negotiation. Offsets, sizes, and total size are all compile-time constants derived from `Fields...`.

The packet type is an immutable description of a bit-level structure. Views (`mad::view`, `mad::cview`, `mad::reg::view`, `mad::reg::xview`, and their const variants) are the runtime bindings that apply that description to some base address.

Validated by tests/layout/packing_offsets_golden.cpp.

## 2. Field kinds and their declared sizes

madpacket has four field kinds.

An integer field is `mad::int_field<Name, Bits, Signed, EndianTag>`. Its declared size is exactly `Bits` bits, where `Bits` is in 1..64. Integer fields are bit-addressable.

A bytes field is `mad::bytes_field<Name, N>`. Its declared size is exactly `N * 8` bits. Bytes fields are byte-addressable (they return a pointer and size), and they are not addressable at bit granularity by the public API.

A padding field is `mad::pad_bits<Bits>` or `mad::pad_bytes<N>`. Its declared size is exactly the padding amount. Padding fields are not addressable and do not produce a value.

A subpacket field is `mad::subpacket_field<Packet, Name>`. Its declared size is exactly `Packet::total_bits`. Subpacket fields are byte-addressable in the sense that a nested view is formed by adding a whole-byte offset to the parent base pointer.

Nothing in the implementation inserts or synthesizes padding. Every bit in the layout is accounted for by one of the explicit fields, including explicit pad fields.

Validated by tests/layout/packing_offsets_golden.cpp.

## 3. Packing rule: contiguous concatenation in declaration order

The packing model is “concatenate fields in order, bit-by-bit”.

For a packet `P = mad::packet<F0, F1, F2, ...>`, the bit offset of field `Fi` is defined as the sum of `Fj::bits` for all `j < i`. The first field’s bit offset is 0.

There is no implicit alignment. A field may start at any bit offset, including offsets not divisible by 8, and including offsets that cause the field to straddle bytes. This is a deliberate design point: the library’s primary purpose is to model wire formats and register layouts that are not naturally aligned.

Because packing is purely by declared bit size, the layout is stable and transparent: there is no ABI, no compiler-dependent padding, and no “struct rules” involved.

Validated by tests/layout/packing_offsets_golden.cpp.

## 4. Total size: total_bits and total_bytes

`Packet::total_bits` is the sum of all `Fields::bits`.

`Packet::total_bytes` is computed as `(total_bits + 7) >> 3`, which is the ceiling of total_bits / 8.

This implies that if `total_bits` is not a multiple of 8, the final byte of the backing buffer contains some high-order bits that are not part of any field. Those bits exist only because the buffer is byte-addressed and must round up to a whole number of bytes.

The layout contract is that no field ever claims those unused bits, because by construction there is no field whose bit range extends beyond `total_bits`. Reads and writes may touch the containing bytes of a field (in particular, read-modify-write updates for bitfields preserve neighboring bits within the touched bytes), but the field’s logical value is defined only by its declared bit range.

This is the reason the minimum safe backing buffer size is `Packet::total_bytes`. If you supply fewer bytes, the implementation will perform out-of-bounds loads and stores for fields near the end of the layout.

Validated by tests/layout/packing_offsets_golden.cpp and tests/bitfields/window_minimality.cpp and tests/bounds/make_view_asserts.cpp.

## 5. The compile-time offset tables exposed by mad::packet

`mad::packet` exposes two compile-time arrays, both indexed by field index in the same order as `Fields...`.

`Packet::offsets_bits[i]` is the bit offset of field i.

`Packet::sizes_bits[i]` is the bit size of field i, equal to `field_t<i>::bits`.

These arrays are computed in a `consteval` context using a fold expression and stored as `std::array<std::size_t, field_count>`.

The contract is that these arrays reflect the packing rule exactly and are not “advisory”. All access paths use these offsets and sizes (directly or indirectly) to determine which bytes to touch.

Validated by tests/layout/packing_offsets_golden.cpp.

## 6. Name lookup and uniqueness are part of the layout model

Field names are `mad::fixed_string` non-type template parameters.

Within a single `mad::packet<...>`, all named fields must have unique names. Duplicate names are ill-formed and rejected at compile time.

The packet provides `Packet::index_of<Name>` and `Packet::has<Name>` as compile-time queries. Access APIs (`get<Name>`, `set<Name>`) are compile-time name lookups; requesting a name not present in the packet is ill-formed and rejected at compile time.

These are layout-level constraints because they affect whether a particular layout description can be formed and addressed.

Validated by tests/names/unique_names_required_compile_fail.cpp and tests/names/name_not_found_compile_fail.cpp.

## 7. Byte-addressed field types require byte-granularity containment

The implementation has two categories of access.

Bit-addressed access is used for integer bitfields. It can begin and end at arbitrary bit offsets.

Byte-addressed access is used for bytes fields and subpacket fields. The implementation forms a pointer by computing `byte_offset = bit_offset >> 3` and adding it to the base pointer. It does not incorporate any intra-byte shift.

Therefore, the layout contract requires byte-granularity containment for bytes fields and subpacket fields.

A bytes field must start on a byte boundary. Concretely, its bit offset must be divisible by 8. If it does not, `bit_offset >> 3` points to the wrong byte, and the returned bytes reference does not correspond to the declared bits of the field. The library does not currently reject this construction at compile time, so violating this rule is a layout-level undefined behavior that will manifest as silent corruption.

A subpacket field must start on a byte boundary for the same reason: the nested view is created by adding a whole-byte offset.

A subpacket field must also be a whole number of bytes in size. Concretely, the nested packet type must satisfy `SubPacket::total_bits % 8 == 0`.

This size requirement is not aesthetic; it is required for correctness. If a subpacket’s total bits are not a multiple of 8, then `SubPacket::total_bytes` rounds up and the nested view’s last byte overlaps bits that belong to the parent layout after the subpacket field. Any nested access that touches that last byte (including read-modify-write within the nested packet) can read or write bits outside the subpacket region and thereby alias the parent’s following fields.

The library does not currently statically reject subpackets whose total_bits is not byte-multiple, so this rule is also a caller-enforced layout precondition. If you violate it, behavior is undefined at the library level even if it “seems to work” for a subset of accesses.

Validated by tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp.

## 8. Scalar-integer “fast path” eligibility is a layout property

Some access paths treat certain integer fields as byte-aligned scalars and operate on whole bytes, while other integer fields are treated as bitfields and operate on a minimal byte window with bit extraction or insertion.

Whether a field is treated as a byte-aligned scalar is determined purely by layout facts: its bit offset and its declared bit width.

A field is a byte-aligned scalar integer field if and only if its bit offset is divisible by 8 and its width is exactly 8, 16, 32, or 64 bits.

All other integer fields are treated as bitfields, including any integer field whose width is not in {8,16,32,64}, even if it starts on a byte boundary.

This classification matters structurally because byte-aligned scalar access touches exactly the field’s bytes, while bitfield access touches a minimal containing byte window and performs read-modify-write within that window.

The layout contract is not “you will always get the fast path”. The layout contract is that the bytes and bits touched by the library are determined by this classification and by the bit numbering rules in docs/01_semantics_contract.md.

Validated by tests/int/get_unsigned_zero_extend.cpp and tests/bitfields/window_minimality.cpp and tests/endian/reject_non_scalar_endian_tag_compile_fail.cpp.

## 9. Nested layouts: offset composition for subpackets

A subpacket field occupies exactly `SubPacket::total_bits` bits in the parent packet’s packing stream.

The nested view returned by `get<SubName>()` is formed by taking the parent base pointer and adding the subpacket field’s byte offset. Inside the nested view, all offsets are computed relative to that nested base pointer, using the nested packet’s own `offsets_bits`.

Structurally, this is “offset composition”: the absolute bit position of a nested field inside the parent buffer is:

parent_subpacket_bit_offset + nested_field_bit_offset

This is only a meaningful mapping if the subpacket starts on a byte boundary and the subpacket size is a whole number of bytes, as specified in the byte-granularity containment rule. If those conditions hold, the nested packet’s bytes are a contiguous byte slice of the parent packet’s bytes.

Validated by tests/layout/subpacket_offsets_golden.cpp.
## 10. Layout validation is enforced at packet formation time

`mad::packet` performs structural validation in a `consteval` context and rejects invalid layouts via `static_assert` during type formation.

The validation enforces that bytes/subpacket fields must start on a byte boundary (`bit_offset % 8 == 0`), subpacket fields require `SubPacket::total_bits % 8 == 0` (whole-byte sized) and non-native-endian integer fields must be byte-aligned and 8/16/32/64 bits

These checks intentionally fire even if a field is never accessed, preventing “latent invalid layout” types from existing.

Runtime preconditions still exist for view construction (e.g., backing buffer size checks in `make_view`), and those remain separate from layout validation.
