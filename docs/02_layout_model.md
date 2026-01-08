````markdown
# Layout Model

This document defines the layout rules of `mad::packet<Fields...>`: how field bit offsets are computed, what it means for a packet to have a given “total size”, how nested packets (subpackets) compose their offsets, and which structural invariants are required for the implementation to be correct.

This document is intentionally narrow. It does not restate bit numbering within bytes, endianness semantics, truncation and sign/zero extension rules, or volatile ordering. Those are specified in `docs/01_semantics_contract.md`. This document is about the compile-time geometry of bits and bytes.

When this document says that something “must” be true, it is describing an invariant of the model. In current madpacket, these invariants are enforced at packet formation time via `static_assert`, so an invalid layout is not a runtime concern; it is an ill-formed type.

## 1. What a layout is

A “layout” is the compile-time result of instantiating `mad::packet<Fields...>`.

The template parameter pack `Fields...` is the entire schema. There is no runtime schema object, no dynamic size negotiation, and no reflection-driven discovery. The offsets, sizes, and totals that describe the layout are compile-time constants derived solely from the declared field descriptors.

A `mad::packet` is therefore an immutable, purely type-level description of a bit-level structure. Runtime objects do not “contain” fields. Runtime objects are views that bind a packet description to a concrete address range. The layout rules in this document explain how the packet description maps field names to bit positions; the view APIs then apply that mapping to a particular buffer or register.

In particular, a layout is not a C++ ABI struct. There is no compiler-inserted padding, no platform ABI alignment, and no dependence on the host compiler’s layout rules. The layout exists entirely in the madpacket domain: it is the concatenation of declared field bit widths.

The “golden” tests that validate the model are `tests/layout/packing_offsets_golden.cpp` for basic packing behavior and `tests/layout/subpacket_offsets_golden.cpp` for nested subpacket offset composition.

## 2. Field descriptors and declared bit sizes

Every field descriptor in `Fields...` contributes an exact number of bits to the packing stream. The layout model does not create padding implicitly, does not round field sizes, and does not align fields unless you explicitly model alignment with padding fields.

The library supports addressable integer fields, addressable byte ranges, explicit padding, and addressable subpackets.

An integer field is a named, fixed-width integer region that is bit-addressable. At the type level it is represented by an integer field descriptor (internally an `int_field`-style descriptor) and it declares a bit width `Bits` in the range 1 through 64 inclusive. The declared size of the field is exactly `Bits` bits. Integer fields are the only fields that madpacket allows to start and end at arbitrary bit offsets, including offsets that are not divisible by 8 and widths that straddle bytes. This is the mechanism used to model wire-format bitfields and irregular register layouts.

A bytes field is a named, fixed-length contiguous byte region. At the type level it is represented by a bytes field descriptor (internally a `bytes_field`-style descriptor) parameterized by a byte count `N`. The declared size of the field is exactly `N * 8` bits. The crucial distinction is not the size, which is still expressed in bits for layout computation; the distinction is how the runtime API addresses it. A bytes field is returned as a pointer and a size in bytes, not as a bit-addressable region.

A padding field is an unnamed, non-addressable region that contributes bits to the packing stream but does not produce an addressable field. Padding exists only because you wrote it. If you need a field to begin at a particular bit or byte boundary, you model that boundary explicitly by inserting `pad_bits<...>` or `pad_bytes<...>` in the field list. The declared size of a padding field is exactly the amount of padding you specify.

A subpacket field is a named field whose payload is itself another `mad::packet` type. At the type level it is represented by a subpacket descriptor (internally a `subpacket_field`-style descriptor) holding a packet type `SubPacket` and a name. The declared size of the subpacket field is exactly `SubPacket::total_bits`. The meaning of “subpacket” is not “inline struct” in the C++ sense; it is “a nested layout that is addressed via a nested view whose base pointer is computed from the parent buffer”.

The fact that sizes are expressed in bits is fundamental. Even for bytes and subpackets, where runtime access is byte-oriented, the layout model first computes their bit position and their bit extent. Byte-oriented access is then derived from those bit facts, and that derivation imposes strict invariants, described later, that madpacket enforces.

## 3. Packing rule: concatenation in declaration order

The packing model is “concatenate fields in declaration order, bit by bit”.

Let `P` be `mad::packet<F0, F1, F2, ...>`. Define `bits(Fi)` as the declared number of bits contributed by field descriptor `Fi`. The bit offset of `Fi` is defined as the sum of the bit widths of all preceding fields:

`offset_bits(F0) = 0`

`offset_bits(Fi) = bits(F0) + bits(F1) + ... + bits(F(i-1))` for `i > 0`

This is a pure prefix sum. There is no implicit alignment. If you place an integer bitfield after a 3-bit field, it begins at bit offset 3. If you place a padding field of 5 bits, the next field begins at the sum of those 5 bits plus whatever came before. If you want byte alignment, you model it by adding explicit padding such that the next field’s offset becomes divisible by 8.

Because the rule is this simple, the layout is transparent and stable. Two different compilers cannot “disagree” about it, because it is not delegated to the compiler. It is computed directly by `mad::packet` as a `consteval` result.

The golden tests in `tests/layout/packing_offsets_golden.cpp` are written to ensure that offsets computed this way match the byte patterns produced by `get` and `set` for representative cases, including fields that straddle byte boundaries.

A minimal example that illustrates the rule at compile time looks like this:

```cpp
using P = mad::packet<
  mad::ubits<3, "a">,
  mad::ubits<5, "b">,
  mad::u8<"c">
>;

static_assert(P::offsets_bits[0] == 0);
static_assert(P::offsets_bits[1] == 3);
static_assert(P::offsets_bits[2] == 8);

static_assert(P::sizes_bits[0] == 3);
static_assert(P::sizes_bits[1] == 5);
static_assert(P::sizes_bits[2] == 8);
````

This example is not “special casing” intra-byte fields. It is exactly the same rule applied uniformly: `b` begins after the 3 bits of `a`, and `c` begins after the 3+5 bits that form the first byte.

## 4. Total size: `total_bits` and `total_bytes`

`Packet::total_bits` is the sum of declared bit sizes of all field descriptors in the packet:

`total_bits = bits(F0) + bits(F1) + ... + bits(Fn-1)`

`Packet::total_bytes` is the number of bytes required to store `total_bits` bits in a byte-addressed buffer, computed as the ceiling of `total_bits / 8`:

`total_bytes = (total_bits + 7) >> 3`

This rounding is not a layout decision; it is an unavoidable consequence of the fact that the backing memory is byte-addressed. If your layout contains 1 bit, you still need at least 1 byte of storage to hold it. If your layout contains 9 bits, you need 2 bytes.

This rounding introduces the concept of trailing unused bits when `total_bits` is not divisible by 8. Those bits exist in the last byte of the buffer but are not part of any declared field, because no declared field’s bit range extends beyond `total_bits`. In the layout model, those bits are outside the packet. They are not addressable by name, and no layout computation assigns them to a field.

This does not mean they are never touched at the machine level. Bitfield updates are typically implemented as read-modify-write on a minimal containing byte window, and a field near the end of the packet may require touching the final byte that also contains trailing unused bits. The semantics contract, as defined in `docs/01_semantics_contract.md`, is that operations preserve bits outside the logical range of the field within any bytes that are touched. That preservation property is what prevents a bitfield write from “bleeding” into neighboring fields or into trailing unused bits.

The minimum safe backing buffer size for a view is `Packet::total_bytes`. If you supply fewer bytes, fields near the end of the packet will necessarily require out-of-bounds loads or stores when the implementation touches the containing bytes. This is not a negotiable property of the model; it follows directly from the definition of `total_bytes` as the ceiling of the total bit size.

Runtime validation of buffer size belongs to view construction and is validated by tests such as `tests/bounds/make_view_asserts.cpp`. The definition of the totals belongs here, because view checks must be derived from these totals, and because subpackets and bytes fields also rely on the relationship between bit totals and byte totals.

## 5. The compile-time offset and size tables exposed by `mad::packet`

A `mad::packet` exposes two compile-time arrays that represent the layout in an indexable, non-name-based form.

`Packet::offsets_bits[i]` is the bit offset of the i-th field in the template parameter pack, computed by the packing rule described earlier.

`Packet::sizes_bits[i]` is the declared size in bits of the i-th field in the template parameter pack, equal to that field descriptor’s `bits` constant.

These arrays are indexed in the exact declaration order of `Fields...`. The i-th element corresponds to the i-th descriptor in `Fields...`. There is no reordering, grouping, or canonicalization. A packet is not allowed to “optimize” its internal ordering, because that would break the foundational guarantee that the declaration order is the layout order.

The packet also exposes `Packet::field_count`, which is the number of descriptors in `Fields...`. The arrays are `std::array<std::size_t, field_count>`, and they exist as compile-time constants so they can be consumed by metaprogramming code, by tests, and by the library’s own internal access machinery.

The important point is that these arrays are not “advisory metadata”. They are part of the layout’s public contract. All access paths, whether name-based or index-based internally, derive the touched bytes and bit positions from these offsets and sizes. The golden tests assert specific values in these arrays for representative layouts precisely because they are the ground truth of the model.

## 6. Names, lookup, and uniqueness as layout constraints

Field naming is part of layout formation, not merely part of the runtime API. A packet must be name-addressable in a single, unambiguous way.

Field names are compile-time strings, represented as `mad::fixed_string` non-type template parameters. A packet’s fields are therefore identified by compile-time values, not by runtime strings. This is what makes `get<"name">()` and `set<"name">()` compile-time selection operations rather than runtime dispatch.

Within a single `mad::packet<...>`, all named fields must have unique names. If two descriptors introduce the same name, the packet type is ill-formed and is rejected at compile time. The reason this is a layout rule rather than an API nicety is that name collisions would make the packet’s address space ambiguous. A layout description that cannot be addressed unambiguously is not a valid layout in the madpacket model.

The packet provides compile-time name queries such as `Packet::has<Name>` and `Packet::index_of<Name>`. These queries are part of the layout’s compile-time interface. They are used by the view APIs to reject invalid field names at compile time, and they are used by tests to verify name lookup behavior.

The compile-fail tests under `tests/names/` validate both uniqueness enforcement and “name not found” rejection.

## 7. Byte-addressed fields and the byte-alignment invariants

The layout model allows fields to begin at arbitrary bit offsets in general, but byte-addressed field kinds impose stricter requirements. These requirements exist because of how the runtime API constructs pointers.

For a bytes field or a subpacket field, the runtime implementation forms a byte pointer by computing a byte offset from the field’s bit offset using `byte_offset = bit_offset >> 3`, and then adding that to the base pointer. This computation discards any intra-byte bit position. There is no intra-byte shifting step for byte-addressed access, because the API of a bytes field is not “a bit slice”; it is “a pointer to bytes”.

As a consequence, a bytes field must start on a byte boundary. Concretely, its bit offset must be divisible by 8. If it did not, then `bit_offset >> 3` would point at the byte containing the field’s first bits rather than the byte that begins the field, and the returned pointer would not describe the declared field region. That would be a structural mismatch between the type-level layout and the runtime address returned by the API.

A subpacket field must also start on a byte boundary for the same reason. The nested view base pointer is computed by adding `bit_offset >> 3` bytes to the parent base. If the subpacket field began at a non-byte boundary, the nested view would begin at the wrong address, and every nested field access would be offset incorrectly.

In madpacket as it exists now, these rules are enforced at packet formation time. Attempting to declare a bytes field or subpacket field at a misaligned bit offset is rejected by `static_assert` during the instantiation of the packet type.

A concrete example that is structurally invalid is this:

```cpp
using Bad = mad::packet<
  mad::ubits<1, "flag">,
  mad::bytes<2, "payload"> // invalid: payload starts at bit offset 1
>;
```

This fails because the bytes field’s bit offset is 1, which is not divisible by 8. The library rejects this layout because there is no correct way to implement `get<"payload">()` as a byte pointer without violating the declared bit geometry.

The same alignment rule applies to subpackets:

```cpp
using Inner = mad::packet<
  mad::u8<"x">
>;

using Bad = mad::packet<
  mad::ubits<3, "hdr">,
  mad::subpacket<Inner, "inner"> // invalid: inner starts at bit offset 3
>;
```

This fails for the same reason: the nested view would necessarily compute a base pointer that cannot represent an offset of 3 bits.

These rules are validated by `tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp` and are further exercised by the golden subpacket offset test `tests/layout/subpacket_offsets_golden.cpp`, which asserts both compile-time offsets and runtime pointer relationships.

## 8. Subpacket size must be whole-byte sized

Subpackets impose an additional invariant beyond starting alignment: the subpacket’s total size must be a whole number of bytes.

The declared size of a subpacket field is `SubPacket::total_bits`. The nested view is constructed over `SubPacket::total_bytes` bytes, because views are bound to byte-addressed memory ranges. If `SubPacket::total_bits` is not divisible by 8, then `SubPacket::total_bytes` is a rounded-up ceiling, and the nested view necessarily includes a last byte that contains bits that lie beyond the subpacket’s declared bit extent.

That last byte is not harmless. A nested packet is allowed to contain bitfields whose updates are implemented as read-modify-write on the containing byte window. If the nested view’s last byte overlaps with bits that, in the parent layout, belong to fields after the subpacket, then a nested update that touches that last byte can read or write bits outside the subpacket region. At that point, the nested view is not a faithful view of the subpacket field; it is an aliasing view that overlaps adjacent parent fields. The layout model does not allow such overlap.

For that reason, a subpacket field requires `SubPacket::total_bits % 8 == 0`. This is not a performance preference. It is a correctness condition that makes the byte range of the nested view match exactly the subpacket’s region within the parent’s byte buffer.

This requirement is now enforced at packet formation time. A subpacket whose `total_bits` is not divisible by 8 cannot be used as a `subpacket<...>` field, because there is no safe definition of “the nested view’s backing byte range” that both includes all subpacket bits and excludes all bits outside the subpacket.

The golden test `tests/layout/subpacket_offsets_golden.cpp` is specifically designed to validate subpacket composition under the conditions that make it meaningful: the subpacket begins on a byte boundary and occupies a whole number of bytes, so the nested view’s base pointer and the nested field offsets compose into correct absolute bit positions.

## 9. Integer fields, scalar eligibility, and non-native-endian restrictions

Integer fields are always declared in bits, but the implementation distinguishes two access categories: scalar-byte access and bitfield access. This distinction matters for both correctness constraints and performance characteristics, and it is purely a function of the layout.

An integer field is treated as a byte-aligned scalar integer field if and only if its bit offset is divisible by 8 and its bit width is exactly 8, 16, 32, or 64. In that case, the field occupies an integral number of bytes, begins at a byte boundary, and can be implemented as operations on those bytes without requiring bit extraction and insertion. This is the category that supports explicit endianness conversions on non-native-endian representations, because endianness in madpacket is defined over byte sequences.

All other integer fields are treated as bitfields. This includes integer fields whose width is not one of 8, 16, 32, or 64, even if the field begins at a byte boundary. It also includes 8/16/32/64-bit fields that begin at a non-byte boundary. Bitfield access operates on a minimal containing byte window and uses the bit numbering rules defined in `docs/01_semantics_contract.md` to extract or insert the field’s logical value.

Non-native-endian integer fields impose stricter requirements, and these are now enforced structurally at packet formation time. If an integer field is declared with a non-native endianness tag, then it must be byte-aligned, and its width must be exactly 8, 16, 32, or 64. The reason is not arbitrary. Endianness conversion is defined on whole bytes, and the implementation’s non-native-endian path necessarily treats the field as a sequence of bytes. If the field is not byte-aligned or not a whole number of bytes, there is no coherent definition of “the field’s byte order” that does not partially include neighboring bits.

A representative invalid declaration is this:

```cpp
using Bad = mad::packet<
  mad::ubits<3, "a">,
  mad::u16<"v", mad::endian::big> // invalid: v starts at bit offset 3
>;
```

This fails because a non-native-endian integer must start on a byte boundary. The model requires that `bit_offset % 8 == 0` for such fields, and the library enforces it.

Another invalid declaration is this:

```cpp
using Bad = mad::packet<
  mad::uint_bits<12, "v", mad::endian::big> // invalid: 12-bit non-native-endian
>;
```

This fails because a non-native-endian integer must be one of 8, 16, 32, or 64 bits. Endianness conversion in madpacket is defined for byte sequences that correspond to standard integer widths; applying it to a 12-bit quantity would require defining how partial-byte fields are byte-ordered, which would contradict the core bitfield model and would create ambiguous behavior at byte boundaries.

These restrictions are part of the layout model because they determine whether a packet type can exist. They are not “usage errors” that only show up when you call `get` or `set`; they are rejected when the packet is formed, even if the field would never be accessed.

Compile-fail tests under `tests/endian/` validate the rejection of invalid non-native-endian uses, and the validation logic is part of the packet’s formation-time layout validation described later.

## 10. Nested layouts: offset composition for subpackets

A subpacket field occupies exactly `SubPacket::total_bits` bits in the parent packet’s packing stream, at the bit offset computed by the parent’s packing rule.

When you access a subpacket field by name, the runtime returns a nested view whose base pointer is computed by taking the parent view’s base pointer and adding the subpacket field’s byte offset, which is `parent_subpacket_bit_offset >> 3`.

Inside the nested view, offsets are computed exactly as they would be for any other packet: by the nested packet’s own packing rule, producing nested offsets relative to the nested base pointer.

The structural meaning of nesting is therefore offset composition. If `parent_off` is the parent subpacket field’s bit offset and `nested_off` is a nested field’s bit offset within the subpacket packet, then the nested field’s absolute bit position in the parent backing buffer is `parent_off + nested_off`.

This equation is not an “implementation detail”. It is what it means for a subpacket to be a nested layout rather than a separately stored object. It is also the reason the alignment and whole-byte-size invariants exist: the nested view is created by a pure byte pointer addition, and nested access is defined by composing bit offsets atop that byte pointer. Without byte alignment and whole-byte subpacket size, the composed mapping would not correspond to a contiguous byte slice of the parent buffer and nested access would alias neighboring bits.

The golden test `tests/layout/subpacket_offsets_golden.cpp` asserts both compile-time offset tables and runtime pointer equality for single-level and two-level nesting. The purpose of that test is to ensure that the model’s offset composition statement matches the actual behavior of nested views.

A representative two-level nesting shape, matching the golden test, looks like this:

```cpp
using Inner = mad::packet<
  mad::ubits<4, "x">,
  mad::ubits<4, "y">,
  mad::u8<"z">
>;

using Sub = mad::packet<
  mad::u8<"pfx">,
  mad::subpacket<Inner, "inner">,
  mad::u8<"sfx">
>;

using P = mad::packet<
  mad::u8<"pre">,
  mad::subpacket<Sub, "sub">,
  mad::u8<"post">
>;
```

In this structure, the nested field `"x"` has an absolute bit position equal to the sum of `"sub"`’s offset within `P`, plus `"inner"`’s offset within `Sub`, plus `"x"`’s offset within `Inner`. The test asserts that this composition is reflected both in offsets tables and in actual byte patterns produced by writes.

## 11. Layout validation is enforced at packet formation time

`mad::packet` performs structural validation in a `consteval` context and rejects invalid layouts via `static_assert` during type formation.

This validation is part of the layout model because it defines what counts as a “valid layout”. A type that violates these rules is not a valid `mad::packet` in the model, and it is not allowed to exist as a well-formed type.

The validation enforces the byte-addressing invariants described earlier. Any bytes field must start on a byte boundary, meaning its computed bit offset must satisfy `bit_offset % 8 == 0`. Any subpacket field must start on a byte boundary for the same reason, and must additionally require that the nested packet’s `total_bits` is a multiple of 8 so that the nested view’s byte range does not overlap bits outside the subpacket region.

The validation also enforces the non-native-endian restrictions for integer fields. Any integer field declared with a non-native endianness tag must be byte-aligned and must have a bit width of exactly 8, 16, 32, or 64. This ensures that the endian conversion path is only instantiated for layouts where the field corresponds to a whole number of bytes with a well-defined byte order.

These checks are intentionally unconditional with respect to usage. The packet is rejected even if you never call `get` or `set` for the offending field. This design choice prevents latent invalid layout types from existing in a codebase and later becoming reachable through refactors or template instantiations.

The test `tests/layout/require_byte_alignment_for_bytes_and_subpacket.cpp` exists specifically to confirm that these invariants are enforced by the type system rather than by convention, and `tests/layout/subpacket_offsets_golden.cpp` exists to confirm that, once these invariants hold, nested pointer formation and offset composition behave exactly as the model states.

## 12. Relationship to runtime view construction

Layout validity is a compile-time property of the packet type. Runtime view validity is a property of binding that packet type to a particular backing memory range.

A view constructor such as `make_view<P>(base, size_bytes)` has runtime preconditions that derive directly from layout facts. In particular, the caller must provide at least `P::total_bytes` bytes of accessible memory starting at `base`. If the provided range is smaller, the view cannot be correct, because some field accesses would require touching bytes beyond the end of the supplied range. The view layer asserts or otherwise defends these bounds as part of runtime safety.

Those runtime checks are not layout rules; they are consequences of layout rules. The layout model defines what `total_bytes` means, defines which fields are byte-addressed and how they form pointers, and defines which bit windows an access may touch. The view layer then checks that the concrete buffer satisfies the minimum requirements implied by those definitions.

Keeping these concerns separate is deliberate. The packet type answers “what is the structure”. The view answers “where is that structure stored”. This document specifies the former with enough precision that the latter can be implemented and reasoned about mechanically.

```
```
