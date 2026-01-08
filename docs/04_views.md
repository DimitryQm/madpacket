# Views

This document defines the ordinary buffer view types `mad::view<Packet>` and `mad::cview<Packet>`, how they are constructed, what operations they expose, and what the bounds model is.

This document is intentionally narrow. It does not restate layout formation, bit numbering, endianness, integer truncation/sign-extension, padding rules, or MMIO semantics. Those topics are specified in other documents. The only semantics repeated here are those that are intrinsic to the view API surface and its bounds contract.


## 1. The two buffer view types

A buffer view binds a packet layout type to a byte pointer. The view does not own the buffer, does not allocate, and does not store a length for the buffer.

There are exactly two ordinary buffer view types, and they differ only by whether the underlying pointer is mutable.

`mad::view<Packet>` is a mutable view. Its `data()` is a `std::byte*`, and it permits `set<...>(...)` for the field kinds that support writing.

`mad::cview<Packet>` is a const view. Its `data()` is a `std::byte const*`, and it rejects all `set<...>(...)` at compile time.

Both are aliases of the same underlying template.

```cpp
namespace mad {
  namespace detail {
    template <typename Packet, bool Mutable>
    class view_base;
  }

  template <typename Packet>
  using view  = detail::view_base<Packet, true>;

  template <typename Packet>
  using cview = detail::view_base<Packet, false>;
}
```

The mutability of the view is a property of the type (`view` versus `cview`), not a property of cv-qualification on the view object. The member functions of `view_base` are `const`-qualified even for mutable views; writing goes through the stored pointer, and “constness of the view object” does not imply “constness of the underlying buffer”.

Validated by tests/api/reject_set_on_const_view_compile_fail.cpp.


## 2. Construction: unchecked direct construction vs checked helper construction

### 2.1. Direct construction is unchecked

Both `mad::view<Packet>` and `mad::cview<Packet>` can be constructed directly from a pointer to the first byte of the packet region.

```cpp
mad::view<P>  v_mut(std::byte* p);
mad::cview<P> v_ro (std::byte const* p);
```

This direct construction performs no bounds check. The view stores only the pointer you pass. If the pointed-to storage is smaller than `Packet::total_bytes`, every subsequent access is outside the library’s contract.

This direct construction is therefore an unchecked operation: it expresses that you are already satisfying the precondition “the buffer contains at least `Packet::total_bytes` bytes starting at `p`”.

Validated by tests/bounds/direct_view_is_unchecked.cpp.


### 2.2. `mad::make_view` asserts the byte-count precondition

The library provides `mad::make_view` as a construction helper that checks the “buffer is large enough” precondition once at the point you create the view.

There are two overloads, matching mutability.

```cpp
template <typename Packet>
constexpr mad::cview<Packet> mad::make_view(std::byte const* data, std::size_t n) noexcept;

template <typename Packet>
constexpr mad::view<Packet>  mad::make_view(std::byte* data, std::size_t n) noexcept;
```

Both overloads perform an assertion `MAD_ASSERT(n >= Packet::total_bytes)` and then return a view constructed from `data`.

This is a one-time check at construction; the returned view still does not retain `n`.

Validated by tests/bounds/make_view_asserts.cpp.


## 3. What a view exposes

A view is designed as a thin binding of `{ Packet layout type, base pointer }`. The API reflects that directly: a view has an address and it has a compile-time size derived from its packet type.

### 3.1. Base pointer and packet size constants

`data()` returns the base pointer passed at construction.

```cpp
mad::view<P> v = /* ... */;
std::byte* p = v.data();
```

`size_bytes()` and `size_bits()` are static queries derived from the packet type.

```cpp
std::size_t nbytes = mad::view<P>::size_bytes();  // equals P::total_bytes
std::size_t nbits  = mad::view<P>::size_bits();   // equals P::total_bits
```

These functions do not depend on any particular instance of a view; the view does not store dynamic size information.

Validated by tests/layout/packing_offsets_golden.cpp.


### 3.2. Named field access: `get<Name>()` and `set<Name>(...)`

The primary access pattern is by compile-time field name.

```cpp
auto x = v.get<"field_name">();
v.set<"field_name">(value);
```

`get<Name>()` is always `constexpr` and `noexcept` and is available on both `mad::view` and `mad::cview`.

`set<Name>(...)` is only available on `mad::view`. Attempting to call it on `mad::cview` is rejected at compile time.

Name lookup is performed at compile time. If the name does not exist in the packet, the call is ill-formed and rejected at compile time. If the name exists but refers to a padding descriptor, the call is ill-formed and rejected at compile time.

Validated by tests/names/name_not_found_compile_fail.cpp and tests/api/reject_get_set_on_pad_compile_fail.cpp and tests/api/reject_set_on_const_view_compile_fail.cpp.

The result type of `get<Name>()` and the legality of `set<Name>(...)` are descriptor-dependent.

For integer fields, `get` returns a widened integer value and `set` is legal on mutable views.

For bytes fields, `get` returns a bytes reference view and `set` is rejected.

For subpacket fields, `get` returns a nested packet view and `set` is rejected.

This descriptor-dependent behavior is specified strictly in docs/03_fields_reference.md and is validated by the compile-fail tests under `tests/api/`.

Validated by tests/api/reject_set_on_bytes_compile_fail.cpp and tests/api/reject_set_on_subpacket_compile_fail.cpp.


### 3.3. Index-based access: `get_i<I>()` and `set_i<I>(...)`

In addition to name-based access, views provide index-based access by field index within the `Packet::field_count` field list.

```cpp
auto x = v.get_i<0>();
v.set_i<0>(value);
```

The index `I` is a compile-time constant. If `I` is out of range (`I >= Packet::field_count`), the call is ill-formed.

The same descriptor-dependent legality rules apply as for the name-based accessors. In particular, padding cannot be accessed and bytes/subpacket fields are not assignable.

This document does not restate those descriptor rules; it only states that the index-based accessors exist and route through the same underlying access implementation as the name-based accessors.


## 4. The bounds contract for ordinary buffer views

The ordinary buffer views `mad::view<Packet>` and `mad::cview<Packet>` do not store a length. For that reason, they cannot perform per-access bounds checks, and they do not attempt to do so.

The bounds model is therefore explicit and simple.

If you construct a view directly from a pointer, you are responsible for ensuring that at least `Packet::total_bytes` bytes are valid to read (for `cview`) or read/write (for `view`) starting at that pointer.

If you construct a view through `mad::make_view(data, n)`, the library asserts the precondition `n >= Packet::total_bytes` at construction time and then returns a view that is otherwise identical to one constructed directly from `data`.

There is no “dynamic slicing” or “tracked window size” in `mad::view` and `mad::cview`. If you need a view type that carries an origin pointer and a bound for offset-following patterns, that is a distinct API under `mad::file::` and is documented in docs/07_file_formats.md rather than here.

Validated by tests/bounds/direct_view_is_unchecked.cpp and tests/bounds/make_view_asserts.cpp.
