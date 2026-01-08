# Fields Reference

This document is a strict reference for the field descriptor types that may appear as template arguments to `mad::packet<...>`. It is written to answer only one question: if you are designing a layout, what can you put in `packet<...>`, and what are the hard, compile-time rules for those descriptors.

This document describes descriptor syntax, declared size contribution, whether a descriptor is bit-addressed or byte-addressed, the compile-time constraints that are enforced (especially those enforced by `packet` formation-time validation), and what operations are legal through `mad::view<Packet>` and `mad::cview<Packet>`.

This document intentionally does not define packing/offset computation rules, bit numbering rules, integer truncation/sign-extension behavior, bounds checking, or any MMIO behavior. Those topics are specified elsewhere.


## 1. Definitions and scope

A “packet layout” is the compile-time type `mad::packet<Fields...>`.

A “field descriptor” (or just “descriptor”) is one of the following types, and only these types, used as a `Fields...` element inside a `mad::packet<...>` specialization.

`mad::int_field<Name, Bits, Signed, EndianTag>`.

`mad::bytes_field<Name, N>`.

`mad::pad_bits<Bits>` and `mad::pad_bytes<N>`.

`mad::subpacket_field<Packet, Name>`.

Any other type used as a `Fields...` element is outside the supported surface.

A “named field” is any descriptor with `static constexpr bool has_name == true`. Named fields have a `static constexpr auto name` and are eligible for name lookup by `view.get<Name>()` / `view.set<Name>(...)`. Padding descriptors are not named fields.

A field is “bit-addressed” if its access path is defined in terms of a bit offset within the packet and a bit width, rather than by identifying a whole-byte region. A field is “byte-addressed” if its access path is defined in terms of a byte offset within the packet and a byte count or nested-byte-region size.


## 2. Packet formation-time validation (field-facing rules)

`mad::packet<...>` performs compile-time validation when the packet type is formed, via a `static_assert(validate())` inside the packet definition. This means certain category errors are rejected as soon as you name the packet type, even if you never construct a view or call any accessors.

Name uniqueness is required. Within a single `mad::packet<...>`, every named field’s `Name` must be unique. If two named fields share the same name, the packet type is ill-formed.

Certain descriptors impose byte-boundary requirements. In particular, `bytes_field` and `subpacket_field` must start on a byte boundary within the containing packet. If they do not, the packet type is ill-formed.

Subpacket descriptors impose a whole-byte-size requirement on the nested packet. A `subpacket_field<Packet, Name>` requires that `Packet::total_bits` is a multiple of 8. If it is not, the containing packet type is ill-formed.

Non-native endianness on integer fields is validated even if the field is never accessed. If an `int_field` uses a non-native endianness tag, then the field must start on a byte boundary and its width must be exactly 8, 16, 32, or 64 bits. If these conditions do not hold, the packet type is ill-formed.

Validated by tests/names/unique_names_required_compile_fail.cpp and tests/names/name_not_found_compile_fail.cpp, and by tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp, and by tests/endian/reject_non_scalar_endian_tag_compile_fail.cpp.


## 3. Integer fields (`mad::int_field` and integer aliases)

### Type syntax

Canonical form.

```cpp
template <mad::fixed_string Name, std::size_t Bits, bool Signed, typename EndianTag = mad::native_endian_t>
struct mad::int_field;
```

Endianness tag types.

```cpp
struct mad::native_endian_t {};
struct mad::little_endian_t {};
struct mad::big_endian_t {};
```

Convenience aliases provided by the header.

```cpp
template <fixed_string Name> using mad::u1  = mad::int_field<Name, 1,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u2  = mad::int_field<Name, 2,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u3  = mad::int_field<Name, 3,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u4  = mad::int_field<Name, 4,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u5  = mad::int_field<Name, 5,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u6  = mad::int_field<Name, 6,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u7  = mad::int_field<Name, 7,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u8  = mad::int_field<Name, 8,  false, mad::native_endian_t>;
template <fixed_string Name> using mad::u16 = mad::int_field<Name, 16, false, mad::native_endian_t>;
template <fixed_string Name> using mad::u32 = mad::int_field<Name, 32, false, mad::native_endian_t>;
template <fixed_string Name> using mad::u64 = mad::int_field<Name, 64, false, mad::native_endian_t>;

template <fixed_string Name> using mad::i8  = mad::int_field<Name, 8,  true, mad::native_endian_t>;
template <fixed_string Name> using mad::i16 = mad::int_field<Name, 16, true, mad::native_endian_t>;
template <fixed_string Name> using mad::i32 = mad::int_field<Name, 32, true, mad::native_endian_t>;
template <fixed_string Name> using mad::i64 = mad::int_field<Name, 64, true, mad::native_endian_t>;

template <fixed_string Name> using mad::le_u16 = mad::int_field<Name, 16, false, mad::little_endian_t>;
template <fixed_string Name> using mad::le_u32 = mad::int_field<Name, 32, false, mad::little_endian_t>;
template <fixed_string Name> using mad::le_u64 = mad::int_field<Name, 64, false, mad::little_endian_t>;

template <fixed_string Name> using mad::be_u16 = mad::int_field<Name, 16, false, mad::big_endian_t>;
template <fixed_string Name> using mad::be_u32 = mad::int_field<Name, 32, false, mad::big_endian_t>;
template <fixed_string Name> using mad::be_u64 = mad::int_field<Name, 64, false, mad::big_endian_t>;

template <std::size_t Bits, fixed_string Name> using mad::ubits = mad::int_field<Name, Bits, false, mad::native_endian_t>;
template <std::size_t Bits, fixed_string Name> using mad::ibits = mad::int_field<Name, Bits, true,  mad::native_endian_t>;
```

### Declared size

An `int_field<Name, Bits, Signed, EndianTag>` contributes exactly `Bits` bits to the packet layout.

### Addressing category

Integer fields can be byte-addressed or bit-addressed depending on how they land in the containing packet.

An integer field is accessed as a byte-addressed scalar integer field when its start bit offset within the packet is a multiple of 8 and its width is exactly 8, 16, 32, or 64 bits.

An integer field is accessed as a bit-addressed integer field in all other cases.

This addressing category is about how the view selects its access path. It does not change the descriptor’s declared size contribution.

### Compile-time constraints

Width is constrained at the descriptor level. `Bits` must be in the range 1 through 64 inclusive, or the field type is ill-formed.

Endianness tags are constrained at the descriptor level. If `Bits` is not a multiple of 8, then `EndianTag` must be `mad::native_endian_t`, or the field type is ill-formed.

Non-native endianness tags are further constrained at packet formation time. If `EndianTag` is not native, then the field must start on a byte boundary in the containing packet, and `Bits` must be exactly 8, 16, 32, or 64, or the packet type is ill-formed.

Name uniqueness is a packet-level constraint. Integer fields are named fields and therefore participate in the “no duplicate names within a packet” rule.

### Legal operations

On `mad::view<Packet>` and `mad::cview<Packet>`, `get<Name>()` is legal for integer fields and returns a widened integer value. For unsigned integer fields it returns `std::uint64_t`. For signed integer fields it returns `std::int64_t`. The return type does not depend on `Bits`.

On `mad::view<Packet>`, `set<Name>(v)` is legal for integer fields.

On `mad::cview<Packet>`, calling `set<...>(...)` is ill-formed for all fields. This is enforced at compile time.

### Endianness legality

Native endianness is always legal for integer fields.

Non-native endianness is only legal when the field width is exactly 8, 16, 32, or 64 bits and the field starts on a byte boundary within its containing packet. If you specify a non-native endianness tag on an integer field and those conditions do not hold, the packet type is ill-formed.

Validated by tests/int/get_unsigned_zero_extend.cpp and tests/int/get_signed_sign_extend.cpp, and by tests/api/reject_set_on_const_view_compile_fail.cpp, and by tests/endian/le_be_roundtrip_scalar.cpp and tests/endian/reject_non_scalar_endian_tag_compile_fail.cpp.


## 4. Bytes fields (`mad::bytes_field` and alias `mad::bytes`)

### Type syntax

Canonical form.

```cpp
template <mad::fixed_string Name, std::size_t N>
struct mad::bytes_field;
```

Convenience alias provided by the header.

```cpp
template <fixed_string Name, std::size_t N>
using mad::bytes = mad::bytes_field<Name, N>;
```

### Declared size

A `bytes_field<Name, N>` contributes exactly `N * 8` bits to the packet layout, equivalently `N` bytes.

### Addressing category

A bytes field is byte-addressed.

### Compile-time constraints

A bytes field must start on a byte boundary within the containing packet, or the packet type is ill-formed.

Name uniqueness is a packet-level constraint. Bytes fields are named fields and therefore participate in the “no duplicate names within a packet” rule.

### Legal operations

On `mad::view<Packet>` and `mad::cview<Packet>`, `get<Name>()` is legal for bytes fields and returns a `mad::bytes_ref<N, Mutable>`, where `Mutable` is `true` for `mad::view` and `false` for `mad::cview`.

The returned `bytes_ref` is the intended access path. It exposes `data()`, `size()`, and `as_span()` for direct byte-region access.

`set<Name>(...)` is not legal for bytes fields. Attempting to call `set<Name>(...)` for a bytes field is rejected at compile time, with a diagnostic that directs you to mutate through the bytes view returned by `get<Name>()`.

### Endianness legality

Endianness tags do not apply to bytes fields.

Validated by tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp and tests/api/reject_set_on_bytes_compile_fail.cpp.


## 5. Padding (`mad::pad_bits` and `mad::pad_bytes`)

### Type syntax

Canonical forms.

```cpp
template <std::size_t Bits>
struct mad::pad_bits;

template <std::size_t N>
struct mad::pad_bytes;  // inherits from pad_bits<N * 8>
```

### Declared size

A `pad_bits<Bits>` contributes exactly `Bits` bits to the packet layout.

A `pad_bytes<N>` contributes exactly `N * 8` bits to the packet layout, equivalently `N` bytes.

### Addressing category

Padding is not addressable. Padding descriptors exist only to consume space in the layout.

### Compile-time constraints

`pad_bits<Bits>` requires `Bits >= 1`, or the type is ill-formed.

`pad_bytes<N>` is defined in terms of `pad_bits<N * 8>` and therefore requires `N >= 1`, or the type is ill-formed.

Padding descriptors are not named fields (`has_name == false`) and therefore do not participate in name uniqueness, and cannot be located by name lookup.

### Legal operations

Padding cannot be accessed through `get<Name>()` or `set<Name>(...)` because padding has no name and is not eligible for name lookup.

Padding has no endianness and no view-level operations.

Validated by tests/layout/packing_offsets_golden.cpp and tests/names/name_not_found_compile_fail.cpp.


## 6. Subpacket fields (`mad::subpacket_field` and alias `mad::subpacket`)

### Type syntax

Canonical form.

```cpp
template <typename Packet, mad::fixed_string Name>
struct mad::subpacket_field;
```

Convenience alias provided by the header.

```cpp
template <typename Packet, fixed_string Name>
using mad::subpacket = mad::subpacket_field<Packet, Name>;
```

### Declared size

A `subpacket_field<Packet, Name>` contributes exactly `Packet::total_bits` bits to the containing packet layout.

### Addressing category

A subpacket field is byte-addressed. The subpacket region is addressed as a contiguous byte region interpreted through the nested packet’s view type.

### Compile-time constraints

A subpacket field must start on a byte boundary within the containing packet, or the containing packet type is ill-formed.

The nested `Packet` must have a whole-byte size. Concretely, `Packet::total_bits` must be a multiple of 8, or the containing packet type is ill-formed.

Name uniqueness is a packet-level constraint. Subpacket fields are named fields and therefore participate in the “no duplicate names within a packet” rule.

### Legal operations

On `mad::view<Outer>` and `mad::cview<Outer>`, `get<Name>()` is legal for subpacket fields and returns a nested view of the subpacket region.

For a mutable outer view, the nested view is mutable. For a const outer view, the nested view is const.

`set<Name>(...)` is not legal for subpacket fields. Attempting to call `set<Name>(...)` for a subpacket field is rejected at compile time, with a diagnostic that directs you to assign via the nested view.

### Endianness legality

Endianness tags do not apply to the subpacket field itself. Any endianness rules apply to integer fields inside the nested packet type, and are validated when those integer fields and the nested packet are formed.

Validated by tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp and tests/api/reject_set_on_subpacket_compile_fail.cpp.
