# Fields Reference

This file is intentionally narrow. It only answers one question: what descriptor types are valid inside `mad::packet<...>`, what each descriptor contributes to the layout in terms of declared size, and what operations you can legally perform on that descriptor through a `mad::view<Packet>` or `mad::cview<Packet>`. It does not restate packing/offset rules, bit numbering, truncation/sign-extension semantics, or any MMIO behavior; those live elsewhere.

All field descriptors in this library are types. You “build a layout” by writing `using P = mad::packet< ...descriptors... >;`. Every descriptor contributes a compile-time `bits` constant (except that pad has no name and is not addressable), and `packet<...>` validates certain invariants at type formation time.

A “named field” is any descriptor with `has_name == true`. Named fields must have unique names within a single `packet<...>`; pad fields do not participate in this uniqueness rule.

The “field name” template parameters in this library are `mad::fixed_string` non-type template parameters. In normal use you will write them as string literals in angle brackets, like `mad::u16<"len">`.

## `mad::int_field<Name, Bits, Signed, EndianTag>`

### Type syntax

Canonical form:

```cpp
template <mad::fixed_string Name, std::size_t Bits, bool Signed, typename EndianTag = mad::native_endian_t>
struct mad::int_field;
````

Common aliases provided by the header (these are strictly type aliases to `mad::int_field`):

```cpp
template <mad::fixed_string Name> using mad::u1  = mad::int_field<Name,  1, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u2  = mad::int_field<Name,  2, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u3  = mad::int_field<Name,  3, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u4  = mad::int_field<Name,  4, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u5  = mad::int_field<Name,  5, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u6  = mad::int_field<Name,  6, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u7  = mad::int_field<Name,  7, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u8  = mad::int_field<Name,  8, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u16 = mad::int_field<Name, 16, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u32 = mad::int_field<Name, 32, false, mad::native_endian_t>;
template <mad::fixed_string Name> using mad::u64 = mad::int_field<Name, 64, false, mad::native_endian_t>;

template <mad::fixed_string Name> using mad::i8  = mad::int_field<Name,  8, true,  mad::native_endian_t>;
template <mad::fixed_string Name> using mad::i16 = mad::int_field<Name, 16, true,  mad::native_endian_t>;
template <mad::fixed_string Name> using mad::i32 = mad::int_field<Name, 32, true,  mad::native_endian_t>;
template <mad::fixed_string Name> using mad::i64 = mad::int_field<Name, 64, true,  mad::native_endian_t>;

template <mad::fixed_string Name> using mad::le_u16 = mad::int_field<Name, 16, false, mad::little_endian_t>;
template <mad::fixed_string Name> using mad::le_u32 = mad::int_field<Name, 32, false, mad::little_endian_t>;
template <mad::fixed_string Name> using mad::le_u64 = mad::int_field<Name, 64, false, mad::little_endian_t>;
template <mad::fixed_string Name> using mad::be_u16 = mad::int_field<Name, 16, false, mad::big_endian_t>;
template <mad::fixed_string Name> using mad::be_u32 = mad::int_field<Name, 32, false, mad::big_endian_t>;
template <mad::fixed_string Name> using mad::be_u64 = mad::int_field<Name, 64, false, mad::big_endian_t>;

template <std::size_t Bits, mad::fixed_string Name> using mad::ubits = mad::int_field<Name, Bits, false, mad::native_endian_t>;
template <std::size_t Bits, mad::fixed_string Name> using mad::ibits = mad::int_field<Name, Bits, true,  mad::native_endian_t>;
```

The endianness tag types used above are part of the public surface:

```cpp
struct mad::native_endian_t {};
struct mad::little_endian_t {};
struct mad::big_endian_t {};
```

### Declared size

An `int_field` contributes exactly `Bits` bits to the layout.

### Addressing category

An `int_field` is always an integer field, but whether access is “byte-addressed scalar” or “bit-addressed bitfield” depends on where the field lands in the packet and on its width.

When the field starts on a byte boundary and its width is exactly 8, 16, 32, or 64 bits, the field is treated as a byte-addressed scalar integer field.

In all other cases, the field is treated as a bit-addressed integer field, including the case where the width is a multiple of 8 but not one of 8/16/32/64.

### Compile-time constraints

These constraints are enforced directly by `mad::int_field` itself.

The width constraint is mandatory: `Bits` must be in the range 1 through 64 inclusive.

The endianness-tag constraint is mandatory at the type level: if `Bits` is not a multiple of 8, the `EndianTag` must be `mad::native_endian_t`. Any attempt to specify `mad::little_endian_t` or `mad::big_endian_t` for a non-byte-multiple width is rejected at compile time.

These constraints are enforced by `mad::packet<...>` validation at packet formation time when the field is instantiated as part of a packet.

If `EndianTag` is not `mad::native_endian_t`, then the field must start on a byte boundary within the packet.

If `EndianTag` is not `mad::native_endian_t`, then the field width must be exactly one of 8, 16, 32, or 64 bits. In other words, even though `Bits % 8 == 0` is necessary to use an explicit endianness tag, it is not sufficient; non-native endianness is only accepted for 8/16/32/64.

Name uniqueness is a packet-level constraint: if two named fields in the same `packet<...>` have the same `Name`, the packet type is ill-formed. Integer fields are named fields and participate in this uniqueness rule.

### Legal operations

Integer fields are accessed through `get<...>()` and `set<...>(...)` on views.

On a `mad::cview<Packet>` or `mad::view<Packet>`, `get<Name>()` is well-formed for integer fields and returns a 64-bit integer value. The return type is `std::uint64_t` for unsigned integer fields and `std::int64_t` for signed integer fields. The return type does not vary with `Bits`.

On a `mad::view<Packet>`, `set<Name>(value)` is well-formed for integer fields. On a `mad::cview<Packet>`, `set<...>(...)` is ill-formed for all fields, including integer fields.

The `set` function template accepts any `value` type that can be converted by the library’s internal conversion to an integer value; in practice, this includes integral types, `bool`, and enums, and also any type for which a `static_cast<std::uint64_t>(value)` is well-formed.

The following snippet is purely about the shape of the API, not about packing rules:

```cpp
using P = mad::packet<
  mad::u16<"len">,
  mad::ibits<5, "tag">
>;

mad::view<P> v = /* constructed elsewhere */;

std::uint64_t len = v.get<"len">();
std::int64_t  tag = v.get<"tag">();

v.set<"len">(123);
v.set<"tag">(-3);
```

### Endianness legality

Endianness is part of the integer field type via `EndianTag`.

Native endianness is always legal for integer fields. All `u1..u64`, `i8..i64`, `ubits`, and `ibits` aliases are native-endian by construction.

Non-native endianness is only legal when all of the following are true.

The field width is exactly 8, 16, 32, or 64 bits.

The field starts on a byte boundary within the packet.

The field uses `mad::little_endian_t` or `mad::big_endian_t` as its `EndianTag`.

The header provides `le_u16`, `le_u32`, `le_u64`, `be_u16`, `be_u32`, and `be_u64` as ready-made non-native-endian unsigned integer aliases. If you need a signed non-native-endian integer field, you must spell the canonical form directly, and it is still subject to the same legality constraints above:

```cpp
using S = mad::int_field<"s16_be", 16, true, mad::big_endian_t>;
```

## `mad::bytes_field<Name, N>`

### Type syntax

Canonical form:

```cpp
template <mad::fixed_string Name, std::size_t N>
struct mad::bytes_field;
```

Alias provided by the header:

```cpp
template <mad::fixed_string Name, std::size_t N> using mad::bytes = mad::bytes_field<Name, N>;
```

### Declared size

A `bytes_field` contributes exactly `N * 8` bits to the layout, equivalently `N` bytes.

### Addressing category

A `bytes_field` is byte-addressed. It is treated as an addressable contiguous byte region.

### Compile-time constraints

`mad::bytes_field` itself does not impose a minimum value for `N` at the type level.

`mad::packet<...>` validation enforces a placement constraint: a bytes field must start on a byte boundary within the packet. If it does not, the packet type is ill-formed.

Name uniqueness is a packet-level constraint: bytes fields are named fields and therefore participate in the “no duplicate field name” rule.

### Legal operations

Bytes fields are readable via `get<Name>()` and are not assignable via `set<Name>(...)`.

On a `mad::cview<Packet>` or `mad::view<Packet>`, `get<Name>()` for a bytes field returns a `mad::bytes_ref<N, Mutable>` where `Mutable` matches the view’s mutability.

That returned `bytes_ref` is the only supported way to access the bytes field. It exposes `.data()`, `.size()`, and `.as_span()`.

On any view, attempting to call `set<Name>(...)` for a bytes field is ill-formed and is rejected at compile time with a diagnostic indicating that you must write through the returned bytes view.

A minimal shape-only example:

```cpp
using P = mad::packet<
  mad::bytes<"payload", 16>
>;

mad::view<P> v = /* constructed elsewhere */;

auto payload = v.get<"payload">();     // type is mad::bytes_ref<16, true>
std::byte* p = payload.data();
std::size_t n = payload.size();
auto s = payload.as_span();
```

### Endianness legality

Endianness tags do not apply to bytes fields. A bytes field has no endianness parameter and no endianness-related legality rules.

## `mad::pad_bits<Bits>` and `mad::pad_bytes<N>`

### Type syntax

Canonical forms:

```cpp
template <std::size_t Bits>
struct mad::pad_bits;

template <std::size_t N>
struct mad::pad_bytes;  // defined as pad_bits<N * 8>
```

There is no named alias distinct from these types; `pad_bytes<N>` already is the “friendly” byte-count spelling.

### Declared size

`pad_bits<Bits>` contributes exactly `Bits` bits to the layout.

`pad_bytes<N>` contributes exactly `N * 8` bits to the layout, equivalently `N` bytes.

### Addressing category

Pad fields are not addressable. They exist only to consume space in the layout.

### Compile-time constraints

`pad_bits<Bits>` enforces that `Bits >= 1` at compile time.

Because `pad_bytes<N>` is defined in terms of `pad_bits<N * 8>`, it enforces the equivalent constraint that `N >= 1`.

Pad fields have `has_name == false` and do not participate in packet name uniqueness.

### Legal operations

Pad fields cannot be accessed. They are intentionally not retrievable or assignable.

On any view type, attempting to `get<...>()` a pad field is ill-formed.

On any view type, attempting to `set<...>(...)` a pad field is ill-formed.

Pad fields are not named, so they are not selected by `get<Name>()` / `set<Name>()` in the first place; they exist only in the descriptor list.

A shape-only example:

```cpp
using P = mad::packet<
  mad::u8<"a">,
  mad::pad_bits<3>,
  mad::ubits<5, "b">,
  mad::pad_bytes<2>,
  mad::u16<"c">
>;
```

### Endianness legality

Endianness does not apply to pad fields.

## `mad::subpacket_field<Packet, Name>`

### Type syntax

Canonical form:

```cpp
template <typename Packet, mad::fixed_string Name>
struct mad::subpacket_field;
```

Alias provided by the header:

```cpp
template <typename Packet, mad::fixed_string Name> using mad::subpacket = mad::subpacket_field<Packet, Name>;
```

### Declared size

A `subpacket_field<Packet, Name>` contributes exactly `Packet::total_bits` bits to the layout, where `Packet` is the nested packet type.

### Addressing category

A subpacket field is byte-addressed. It is treated as an addressable contiguous region interpreted through the nested packet’s view type.

### Compile-time constraints

The nested `Packet` type must be a packet-like type that provides `total_bits` as a usable compile-time constant, because the field’s own `bits` is defined as `Packet::total_bits`. In practical use, `Packet` is expected to be a `mad::packet<...>` specialization.

`mad::packet<...>` validation enforces two placement/size constraints for subpacket fields.

A subpacket field must start on a byte boundary within the outer packet. If it does not, the outer packet type is ill-formed.

The nested `Packet` must have a whole-byte size. Concretely, `Packet::total_bits` must be a multiple of 8. If it is not, the outer packet type is ill-formed.

Name uniqueness is a packet-level constraint: subpacket fields are named fields and therefore participate in the “no duplicate field name” rule.

### Legal operations

Subpacket fields are readable via `get<Name>()` and are not assignable via `set<Name>(...)`.

On a `mad::cview<Outer>` or `mad::view<Outer>`, `get<Name>()` for a subpacket field returns a nested view over the subpacket region. The returned view type is `mad::detail::view_base<Packet, Mutable>`, which is the same shape as `mad::view<Packet>` for mutable outer views and `mad::cview<Packet>` for const outer views.

On any view, attempting to call `set<Name>(...)` for a subpacket field is ill-formed and is rejected at compile time with a diagnostic indicating you must assign via the nested view.

A minimal shape-only example:

```cpp
using Header = mad::packet<
  mad::u16<"type">,
  mad::u16<"len">
>;

using Frame = mad::packet<
  mad::subpacket<Header, "hdr">,
  mad::bytes<"payload", 32>
>;

mad::view<Frame> f = /* constructed elsewhere */;

auto h = f.get<"hdr">();        // nested view (mutable)
std::uint64_t t = h.get<"type">();
h.set<"len">(12);
```

### Endianness legality

Endianness legality is not a property of `subpacket_field` itself. Any endianness rules apply to the integer fields inside the nested `Packet`, and are validated when that nested packet is formed and when it is used as a subpacket as described above.

## One place where the packet enforces descriptor legality

The constraints in this document that are described as “packet validation” are enforced by `mad::packet<...>` at type formation time via an internal `static_assert(validate())`. In practical terms, you get a compile-time error as soon as you form the packet type, even if you never construct a view or call `get`/`set`.

This matters most for three cases.

Placing `bytes_field` or `subpacket_field` at a non-byte boundary is rejected immediately.

Using a non-native endianness tag on an integer field is rejected unless the field is byte-aligned and the width is exactly 8/16/32/64.

Nesting a subpacket whose `total_bits` is not a multiple of 8 is rejected immediately.
