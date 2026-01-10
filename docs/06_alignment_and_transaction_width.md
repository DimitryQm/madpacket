# Alignment and Transaction Width (xview / cfg)

## 0) Scope and non-goals

This document is about the *MMIO policy surface* for `mad::reg::xview` / `mad::reg::xcview` and the configuration type `mad::reg::cfg<>`.

It defines, precisely, what alignment is enforced (and how), and how transaction width is selected (and how that interacts with capability masks, strict mode, and hardwidth).

It intentionally does not restate bit numbering, endianness, truncation/sign-extension, or layout formation rules; those are specified in docs/01_semantics_contract.md and docs/02_layout_model.md, and the MMIO volatility/barrier/RMW model is specified in docs/05_mmio.md.

It does not attempt to model specific peripheral behavior or vendor rules; it only states what *madpacket* will do and what it will not promise.

It mentions `mad::reg::view` / `mad::reg::cview` only when needed to contrast “fixed behavior” versus “policy-driven behavior”.


## 1) Definitions

### 1.1 Base pointer and field address

An `xview` is a type that binds a packet layout `Packet` to a base pointer `p` that points at the first byte of the register block.

The *base pointer* is the value returned by `xview.data()`. Its type is a volatile byte pointer (`volatile std::byte*` for mutable, `volatile std::byte const*` for const).

A *field address* is the computed address used for a particular field access. For byte-addressed integer fields it is commonly `base + byte_off`. For bus-word transactions it is commonly a bus-word boundary address like `base + (word_idx * Bus::bytes)`. For bitfield transactions it is derived from `bit_off` and bus word sizing.

This document talks about alignment and width selection in terms of these derived field addresses. The concrete address arithmetic is implementation-defined inside `xget_impl` / `xset_impl`, but the alignment and width rules below are what constrain those internal accesses.

Validated by tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp and tests/mmio_xview/bitfield_fallback_byte_window.cpp.


### 1.2 BaseAlign versus bus alignment

There are two distinct alignment notions that show up in `xview`:

`Cfg::base_align` (referred to as **BaseAlign**) is a compile-time promise about how the *base pointer* is aligned in the real address space. It is used in two ways: it is the alignment that `make_xview` checks, and it is used to enable or disable certain typed volatile fast paths.

`Bus::align` (referred to as **bus alignment**) is the natural alignment of a bus transaction word, derived from the bus word type. For example `bus32` has `Bus::align == alignof(std::uint32_t) == 4`.

BaseAlign is under your control as part of the `Cfg` type. Bus alignment is under the bus type, and in normal usage it is fixed by choosing `mad::reg::bus8`, `bus16`, `bus32`, or `bus64`, or by supplying an equivalent custom bus type.

This document treats both as power-of-two alignment values, because the implementation checks alignment via bit-masking.

Validated by tests/mmio_xview/static_validate_enforce_bus_basealign.cpp and tests/mmio_xview/align_assert_checks.cpp.


### 1.3 Transaction width as a C++ concept

When this document says “transaction width”, it is describing the width of the volatile access unit that the library attempts to use for a read or a write.

In the implementation, “typed volatile” transactions are loads and stores through a `volatile T*` where `sizeof(T)` is one of `{1, 2, 4, 8}`. In other words, the conceptual transaction widths are `{1B, 2B, 4B, 8B}`.

Separately, the implementation always has a *bytewise fallback* path, which is a sequence of `volatile std::byte` loads and stores that assemble or scatter an integer in host-endian order. In terms of “transactions”, that fallback is a series of 1-byte volatile accesses.

This matters because a width policy may “select” a width conceptually, but strict mode and other constraints can force the implementation to fall back to bytewise accesses even when you asked for a wider unit.

Validated by tests/mmio_xview/strict_mmio_fallback_behavior.cpp and tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


### 1.4 Capability masks (read_mask / write_mask)

`xview` selection is guided by two capability masks in the config: `Cfg::read_mask` and `Cfg::write_mask`.

A capability mask is a 4-bit `width_mask_t` that indicates which typed volatile widths are considered allowed.

Bit meaning is by width, not by bit position name:

bit0 set means 1-byte is allowed.

bit1 set means 2-byte is allowed.

bit2 set means 4-byte is allowed.

bit3 set means 8-byte is allowed.

The helper `mask_for_bytes(n)` returns the mask bit that corresponds to exactly `n` bytes, for `n ∈ {1,2,4,8}`. The constant `width_all` is the mask with all four widths enabled.

The library uses these masks to decide whether a given typed volatile width can be chosen for a given access. The masks are *advisory selection inputs*, not a full “hardware transaction prohibition mechanism”, because strict mode and fallback behavior can still cause bytewise volatile operations.

Validated by tests/mmio_xview/native_exact_uses_scalar_width.cpp and tests/mmio_xview/strict_mmio_fallback_behavior.cpp.


### 1.5 Policies

`xview` is parameterized by a `Cfg` type. That `Cfg` includes two policy enumerations that matter here.

Alignment policies:

`align_policy::unchecked`

`align_policy::assert_`

`align_policy::trap`

`align_policy::assume`

Width policies:

`width_policy::native`

`width_policy::enforce_bus`

`width_policy::prefer_bus`

`width_policy::minimal_ok`

These policy values are compile-time constants stored in the `Cfg` type, and are therefore known at compile time inside all access paths.

Validated by tests/mmio_xview/align_unchecked_no_check.cpp and tests/mmio_xview/non_native_path_uses_bus_words.cpp.


## 2) Alignment policies (core)

Alignment checking is performed by `detail2::enforce_alignment<P>(p, a)` where `P` is the configured `align_policy` and `a` is the required alignment. This enforcement is used by `make_xview` to validate base-pointer alignment, and is also used in internal MMIO helpers where bus-word alignment is relevant.

Each policy below describes exactly what the library promises, what it does at runtime, and what happens if the contract is violated.

### 2.1 unchecked

Contract.

No alignment check is performed, and no compiler hint is emitted. The library will proceed as if alignment requirements were satisfied even if they are not.

What can go wrong.

If a real MMIO region requires aligned accesses, misalignment can fault. If the platform splits or transforms misaligned accesses, you can observe multiple bus transactions, partial writes, or other bridge-specific behavior that violates what you intended.

When to use.

Use `unchecked` only when alignment is guaranteed externally by construction and you want absolutely no runtime overhead and no optimizer assumptions.

Validated by tests/mmio_xview/align_unchecked_no_check.cpp.


### 2.2 assert_

Contract.

A runtime alignment check is performed via `MAD_ASSERT`. If the assertion fires, the program terminates according to the project’s assertion mechanism.

Important caveat.

Assertions can be compiled out. If `MAD_ASSERT` becomes a no-op in your build configuration, then this policy degenerates to `unchecked` at runtime. This is not hypothetical; it is a normal deployment choice.

What is checked.

The implementation asserts two things when this policy is used for an alignment requirement `a`:

it asserts that `a` is a power of two.

it asserts that the pointer value is aligned to `a`.

The power-of-two check matters because the alignment predicate is implemented by masking.

Where it is used.

`make_xview` uses this policy to check `Cfg::base_align`, and may additionally use it to check `Bus::align` when `width_policy::enforce_bus` is selected (see section 3).

`mad::reg::make_view` (the non-policy `reg::view`) always uses `MAD_ASSERT` for BaseAlign, but it does not route through this policy system; it is hard-coded.

Validated by tests/mmio_xview/align_assert_checks.cpp and tests/mmio/make_view_basealign_asserts.cpp.


### 2.3 trap

Contract.

A runtime alignment check is performed, and if it fails, the library executes a trap sequence immediately. This is intended to be “fail-fast” and independent of `MAD_ASSERT` and independent of `NDEBUG`.

This policy does not rely on the project assertion macro.

What “trap” means.

On GCC and Clang, the implementation uses `__builtin_trap()`.

On MSVC, the implementation uses `__debugbreak()` where available.

If neither path is available, the implementation uses a forced crash via a volatile null pointer store.

The important property is that a misalignment violation does not silently continue.

What is checked.

This policy checks alignment via `is_aligned(p, a)`. It does not assert that `a` is a power of two; you should treat “a must be a power of two” as a required precondition of configuration.

Validated by tests/mmio_xview/align_trap_traps.cpp.


### 2.4 assume

Contract.

No runtime check is performed that can be relied on for safety. Instead, the implementation emits an optimizer hint that allows the compiler to assume alignment.

If the actual pointer is misaligned at runtime, the behavior is undefined. This is not merely “might fault”; it is “the compiler is permitted to miscompile code because you lied”.

This is the one alignment policy that can miscompile surrounding code, not just the MMIO access itself, because alignment assumptions can propagate to unrelated optimizations.

What “assume” means.

On MSVC, the implementation uses `__assume(...)`.

On GCC and Clang, the implementation uses `__builtin_unreachable()` on the misaligned branch.

On other toolchains, the implementation degrades to no-op, which turns this policy into `unchecked` rather than an assumption.

When to use.

Use `assume` only when the base pointer is provably aligned by construction, for example when it is a properly-aligned pointer returned by a hardware mapping API that guarantees alignment, or when you have performed a checked alignment step and are carrying a proven-aligned pointer forward.

Validated by tests/mmio_xview/align_assume_is_ub_contract.cpp.


## 3) What is actually checked, and where

This section exists to prevent a very common misunderstanding: there is not “one alignment check”, there are multiple *distinct* alignment requirements, and they are enforced in different places.

### 3.1 `make_xview` enforces BaseAlign using the configured align policy

`make_xview<Packet, Cfg>(addr)` calls `enforce_alignment<Cfg::align>(addr, Cfg::base_align)`.

That means BaseAlign enforcement is entirely policy-driven.

If you select `unchecked`, you get no check.

If you select `assert_`, you get a `MAD_ASSERT` check.

If you select `trap`, you get an unconditional trap-on-failure.

If you select `assume`, you get an optimizer assumption and UB on misalignment.

Validated by tests/mmio_xview/align_unchecked_no_check.cpp and tests/mmio_xview/align_assert_checks.cpp and tests/mmio_xview/align_trap_traps.cpp and tests/mmio_xview/align_assume_is_ub_contract.cpp.


### 3.2 `make_xview` may additionally enforce bus alignment under enforce_bus

If and only if `Cfg::width == width_policy::enforce_bus`, `make_xview` additionally calls `enforce_alignment<Cfg::align>(addr, Bus::align)`.

This is intentionally narrower than “always enforce bus alignment”, because other width policies are permitted to fall back to narrower operations and do not require the base pointer itself to be bus-aligned as a hard precondition.

Validated by tests/mmio_xview/static_validate_enforce_bus_basealign.cpp.


### 3.3 `static_validate<Packet, Cfg>()` enforces a compile-time rule for enforce_bus

`xview` performs `static_validate<Packet, Cfg>()` at construction and also in its default constructor path. That function contains a `static_assert` that rejects a configuration that cannot possibly satisfy the bus-alignment enforcement it is requesting.

Concretely, the rule is:

If `Cfg::width == width_policy::enforce_bus`, then `Cfg::base_align >= Bus::align` must hold at compile time.

This is a *compile-time* requirement. It is separate from runtime checks. It is enforced even if you never call `make_xview`.

Validated by tests/mmio_xview/static_validate_enforce_bus_basealign.cpp.


### 3.4 `mad::reg::make_view` (non-xview) always asserts BaseAlign

`mad::reg::view` / `mad::reg::cview` are the simpler MMIO view types. They do not have `cfg<>`, do not have width policies, and do not have alignment policies.

Instead, `mad::reg::make_view<Packet, Bus, BaseAlign>(addr)` always asserts that the base pointer is aligned to `BaseAlign` using `MAD_ASSERT`.

This is relevant when you are comparing `reg::view` and `reg::xview`. The former is “fixed behavior”; the latter is “policy-driven behavior”.

Validated by tests/mmio/make_view_basealign_asserts.cpp.


## 4) Width policy semantics (what it tries first, what it falls back to, what it never promises)

Transaction width selection is performed by a compile-time helper `choose_width<WP>(region_bytes, offset_bytes, bus_bytes, mask)`.

This helper returns a “chosen width” in bytes, which is one of `{1,2,4,8}` when selection succeeds, or `0` when no typed volatile width is selected.

Two details matter more than anything else:

First, `choose_width` does not enforce alignment; it only selects a width. Alignment is handled separately by BaseAlign enforcement and internal bus alignment behavior.

Second, width selection is not the entire story. Strict mode (`MADPACKET_STRICT_MMIO`) can disable typed volatile operations, and internal helpers can fall back to bytewise sequences even when a width was “chosen”.

The subsections below describe the intent and the observable behavior for each policy.

### 4.1 native

Intent.

Use an exact-width typed volatile transaction when possible, because it is the closest match to “register-sized access” and can be the lowest-overhead access for byte-aligned scalar fields.

What it tries first.

For a byte-aligned scalar integer field whose width is exactly 1, 2, 4, or 8 bytes, `native` tries to perform an exact-width access of that size if and only if:

the access size is enabled by the relevant capability mask (`read_mask` for loads, `write_mask` for stores),

the base alignment permits the fast path and the pointer is suitably aligned at runtime, and

strict mode does not disable typed volatile loads/stores for that scalar type.

What it falls back to.

If any of those conditions is not met, the `native` policy falls back to bus-style gather/scatter operations that load or store using the configured bus word helpers and assemble/scatter bytes into the desired field representation.

This fallback may itself degrade to bytewise volatile operations under strict mode.

What it never promises.

It does not promise “exact-width hardware transaction” in the presence of strict mode, insufficient alignment, or toolchain decisions that change how volatile is lowered.

Validated by tests/mmio_xview/native_exact_uses_scalar_width.cpp and tests/mmio_xview/strict_mmio_fallback_behavior.cpp.


### 4.2 enforce_bus

Intent.

Treat the bus word as the unit of transaction, and implement sub-word and cross-word fields in terms of bus-word operations plus RMW when needed.

In other words, you select `enforce_bus` because you want the bus word to be the conceptual access primitive, not the field size.

What it tries first.

For byte-aligned scalar integer fields, `enforce_bus` selects bus-word style operations unconditionally; it does not attempt exact-width scalar transactions as a primary strategy.

For bitfields, `enforce_bus` uses bus-word RMW for the word(s) that contain the bitfield, and will perform two-word RMW when the field straddles a bus boundary, subject to the normal fallback rules for cases that cannot be handled as one- or two-word operations.

What it falls back to.

If strict mode disables typed volatile bus-word accesses and no platform intrinsic is used, the bus-word helpers fall back to bytewise volatile assembly/scatter. If `MADPACKET_MMIO_HARDWIDTH` is enabled, these strict-mode bus-word fallbacks are rejected at compile time, because you requested “bus-word access required” and the build configuration forbids it.

What it never promises.

It does not promise that the compiler and the platform will issue exactly one hardware transaction of size `Bus::bytes` for every access, especially under strict mode or on platforms that lower volatile in surprising ways. It does promise that the algorithm is structured around bus-word units and RMW as needed.

Validated by tests/mmio_xview/static_validate_enforce_bus_basealign.cpp and tests/mmio_xview/non_native_path_uses_bus_words.cpp and tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


### 4.3 prefer_bus

Intent.

Prefer bus-word transactions when the requested region is small enough to fit within a bus word and the bus width is permitted, because that makes it easier to adopt “bus-word as the unit” without globally forbidding narrower accesses.

What it tries first.

If the field region is smaller than or equal to a bus word and the bus word width is enabled in the relevant capability mask, `prefer_bus` selects the bus word width.

If that does not apply, it behaves like `native` in the sense that it will choose an exact-width transaction if allowed, otherwise it chooses the smallest allowed width that is at least as large as the field region.

What it falls back to.

As with the other policies, any typed volatile selection can be defeated by strict mode and by alignment, forcing a bytewise fallback implementation even when a width was selected.

Observable guarantee you can rely on.

When `prefer_bus` causes a promoted store (writing wider than the field), the implementation performs a read-modify-write update and preserves the bytes of the surrounding bus word that are outside the target field region.

That “preserve unrelated bytes during RMW” property is the useful thing users tend to mean by “prefer bus”.

Validated by tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp and tests/bitfields/rmw_preserves_neighbor_bits.cpp and tests/mmio_xview/prefer_bus_policy_behavior.cpp.


### 4.4 minimal_ok

Intent.

Make it possible to run on targets that forbid 1-byte and/or 2-byte typed volatile transactions, while still providing correct access to sub-word fields by using wider transactions plus RMW.

This policy is explicitly about “do not use narrow widths, but still must work”.

What it tries first.

It selects the smallest allowed typed volatile width that is at least as large as the field region size, constrained to be no larger than the bus word size.

If no allowed width exists that satisfies that, it yields “no selection”, and the implementation uses the bus-word gather/scatter fallback.

What it falls back to.

If strict mode disables typed volatile operations, the chosen width can still devolve into bytewise volatility. If you need a hard failure rather than a bytewise fallback in strict mode, you must combine strict mode with `MADPACKET_MMIO_HARDWIDTH` in the configurations where bus-word helpers are required.

What it never promises.

It does not promise that hardware will never see 1-byte transactions in strict mode, because strict mode intentionally forces bytewise operations in several places.

Validated by tests/mmio_xview/minimal_ok_policy_behavior.cpp and tests/mmio_xview/strict_mmio_fallback_behavior.cpp.


## 5) How caps masks interact with width selection

This section answers the questions people ask after they first read about `read_mask` and `write_mask`: are these masks hard prohibitions, do they change the algorithm, and what happens when an exact width is not allowed.

### 5.1 If the exact width is not allowed, what happens?

For byte-aligned scalar integer fields under `width_policy::native`, an exact-width typed volatile access is used only when the exact field width is enabled by the relevant mask.

If the exact width is not enabled, the implementation falls back to the bus-style gather/scatter path for that field. It does not attempt an “alternate typed volatile width” as a general strategy for scalar fields; the non-exact path is intentionally structured around bus-word helpers that assemble/scatter bytes.

For policies other than `native`, the implementation already prefers the bus-style path for scalar fields, so disallowing exact widths mostly matters by preventing the `native` fast path.

Validated by tests/mmio_xview/native_exact_uses_scalar_width.cpp and tests/mmio_xview/non_native_path_uses_bus_words.cpp.


### 5.2 Are masks hard-fail compilation gates, or selection guides?

They are selection guides. If a mask does not allow a particular typed width, the implementation will not choose that typed width in `choose_width`.

However, the absence of a typed width does not necessarily make an access ill-formed. The implementation can and will fall back to different algorithms (bus-word gather/scatter, or bytewise volatility) that do not require the forbidden typed width.

If you require “hard failure when a forbidden width would be needed”, that is not what the masks alone provide. The knob that turns some fallbacks into compile-time failures is `MADPACKET_MMIO_HARDWIDTH`, and it is specifically tied to strict-mode bus-word operations.

Validated by tests/mmio_xview/strict_mmio_fallback_behavior.cpp and tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


### 5.3 What does MADPACKET_MMIO_HARDWIDTH change?

When `MADPACKET_MMIO_HARDWIDTH` is enabled, certain fallback paths are rejected at compile time rather than silently using a bytewise fallback.

Concretely, when strict mode is enabled, bus-word helpers that would otherwise fall back to bytewise operations become ill-formed due to an internal `static_assert` that states that “busword access required, but strict mode forbids it”.

This is the mechanism that prevents “strict mode plus enforce_bus silently becomes a sequence of byte reads/writes”.

Validated by tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


## 6) Strict MMIO mode

Strict MMIO mode is controlled by defining `MADPACKET_STRICT_MMIO`.

The practical effect is that several typed volatile fast paths are disabled. In their place, the implementation uses bytewise volatile loads/stores that mimic memcpy semantics on the host endianness for the integral types being assembled.

This is a real semantic knob because it changes what kinds of volatile operations the compiler is allowed to emit.

Risks introduced by strict mode.

On hardware where 1-byte or 2-byte transactions are forbidden or have side effects, strict mode can be actively dangerous because its fallback implementation is precisely a series of 1-byte volatile transactions.

That is why strict mode has to be considered together with width policy and with `MADPACKET_MMIO_HARDWIDTH`.

How hardwidth interacts.

With `MADPACKET_MMIO_HARDWIDTH`, strict-mode bus-word fallbacks are rejected at compile time, forcing you to choose a configuration that can actually satisfy “typed bus-word volatile” on that build.

Validated by tests/mmio_xview/strict_mmio_fallback_behavior.cpp and tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


## 7) How to choose (decision guide)

If you know your base pointer alignment is correct, and you want maximum performance with the fewest policy obstacles, choose `align_policy::assume` and `width_policy::native`, and set BaseAlign to the strongest alignment you can prove. Do this only when you can prove it, because the consequence of being wrong is undefined behavior and miscompilation.

If you are developing and want alignment mistakes to show up early in debug builds, choose `align_policy::assert_`. Keep in mind that assertions can be compiled out, so do not treat this as a production hard guarantee.

If misalignment is always fatal and you want fail-fast behavior that does not depend on build flags, choose `align_policy::trap`.

If your hardware forbids narrow transactions and you want your algorithm to be structured around bus words, choose `width_policy::enforce_bus`, set BaseAlign to at least `Bus::align`, and set capability masks to allow only bus-word widths. Avoid strict mode in this scenario unless you also enable `MADPACKET_MMIO_HARDWIDTH` and you are certain the toolchain can emit the required typed volatile operations.

If your environment forbids 1-byte and 2-byte typed volatile but you still need to access smaller fields correctly, choose `width_policy::minimal_ok` and configure capability masks appropriately (for example “4-byte only”), so the library uses wider RMW operations instead of narrow typed volatile.

Validated by tests/mmio_xview/align_assume_is_ub_contract.cpp and tests/mmio_xview/static_validate_enforce_bus_basealign.cpp and tests/mmio_xview/minimal_ok_policy_behavior.cpp.


## 8) Examples (compile-ready)

The examples below are intentionally concrete. They compile as C++20 when included in a TU that includes the madpacket single header. They are written to demonstrate only configuration, alignment policy selection, and width selection, not protocol design.

### 8.1 Memory-mapped peripheral registers: 32-bit bus, enforce bus

This example uses a 32-bit bus model and requests bus-word transactions as the unit. It also checks alignment with `assert_` during construction.

```cpp
#include "madpacket.hpp"

using Regs = mad::packet<
  mad::u32<"CTRL">,
  mad::u32<"STAT">,
  mad::u32<"DATA">
>;

using Bus = mad::reg::bus32;

// Allow only 4-byte typed volatile transactions.
using Caps = mad::reg::caps<mad::reg::mask_for_bytes(4), mad::reg::mask_for_bytes(4)>;

// BaseAlign is 4, enforce bus-word behavior, assert alignment at runtime.
using Cfg = mad::reg::cfg<Bus, 4, mad::reg::width_policy::enforce_bus, mad::reg::align_policy::assert_, Caps>;

void example(volatile void* mmio_base) {
  auto v = mad::reg::make_xview<Regs, Cfg>(mmio_base);

  // Read and write as usual; the policy affects the underlying volatile transactions.
  v.set<"CTRL">(0x1u);
  (void)v.get<"STAT">();
}
```

Validated by tests/mmio_xview/static_validate_enforce_bus_basealign.cpp and tests/mmio_xview/non_native_path_uses_bus_words.cpp.


### 8.2 High-performance path: assume + native

This example assumes alignment and uses the native policy to permit exact-width scalar typed volatile fast paths when available.

```cpp
#include "madpacket.hpp"

using P = mad::packet<
  mad::u32<"A">,
  mad::u16<"B">,
  mad::u16<"C">
>;

using Bus = mad::reg::bus32;

// BaseAlign is 4, native widths, compiler assumption about alignment.
using Cfg = mad::reg::cfg<Bus, 4, mad::reg::width_policy::native, mad::reg::align_policy::assume, mad::reg::caps_all>;

std::uint64_t fast_read(volatile void const* mmio_base) {
  auto v = mad::reg::make_xview<P, Cfg>(mmio_base);
  return v.get<"A">();
}
```

This is the configuration that most benefits from “I know my pointer is aligned and I want the compiler to optimize aggressively”. It is also the configuration with the sharpest failure mode if you are wrong.

Validated by tests/mmio_xview/align_assume_is_ub_contract.cpp and tests/mmio_xview/native_exact_uses_scalar_width.cpp.


### 8.3 No 8-bit transactions environment: minimal_ok + 4B-only caps

This example demonstrates a configuration where 1-byte and 2-byte typed volatile accesses are treated as forbidden, but smaller fields must still be writable by using 4-byte transactions plus RMW.

```cpp
#include "madpacket.hpp"

using P = mad::packet<
  mad::u8<"FLAG">,
  mad::pad_bits<8>,
  mad::u16<"MODE">,
  mad::u32<"DATA">
>;

using Bus = mad::reg::bus32;

// Allow only 4-byte typed volatile transactions.
using Caps4Only = mad::reg::caps<mad::reg::mask_for_bytes(4), mad::reg::mask_for_bytes(4)>;

// minimal_ok means "pick the smallest allowed width >= field size", which here means 4B for the sub-4B fields.
using Cfg = mad::reg::cfg<Bus, 4, mad::reg::width_policy::minimal_ok, mad::reg::align_policy::assert_, Caps4Only>;

void write_small_fields(volatile void* mmio_base) {
  auto v = mad::reg::make_xview<P, Cfg>(mmio_base);

  // These are smaller than 4 bytes, but the config makes the engine use 4B transactions with RMW.
  v.set<"FLAG">(1);
  v.set<"MODE">(0x12);
  v.set<"DATA">(0xDEADBEEF);
}
```

Validated by tests/mmio_xview/minimal_ok_policy_behavior.cpp and tests/bitfields/rmw_preserves_neighbor_bits.cpp.


## 9) Validation index

The following test files are used as anchors in this document.

tests/mmio_xview/align_unchecked_no_check.cpp

tests/mmio_xview/align_assert_checks.cpp

tests/mmio/make_view_basealign_asserts.cpp

tests/mmio_xview/align_trap_traps.cpp

tests/mmio_xview/align_assume_is_ub_contract.cpp

tests/mmio_xview/static_validate_enforce_bus_basealign.cpp

tests/mmio_xview/native_exact_uses_scalar_width.cpp

tests/mmio_xview/non_native_path_uses_bus_words.cpp

tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp

tests/mmio_xview/strict_mmio_fallback_behavior.cpp

tests/bitfields/rmw_preserves_neighbor_bits.cpp

tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp

tests/mmio_xview/bitfield_fallback_byte_window.cpp

tests/mmio_xview/prefer_bus_policy_behavior.cpp

tests/mmio_xview/minimal_ok_policy_behavior.cpp
