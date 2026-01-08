# MMIO

This document specifies the operational behavior of madpacket’s volatile MMIO access layer.

The goal here is not to repeat the entire project’s “semantic contract”. Those are already defined at a higher level. The goal here is to describe what changes when the backing storage is a volatile MMIO region: what kinds of volatile accesses the library emits, what width and alignment constraints it can enforce, when it performs read-modify-write sequences, where barriers are placed, and what “forbidden width” means in practice.

Validated by tests/mmio/basic_scalar_endian.cpp, tests/mmio/barrier_placement.cpp, tests/mmio/bitfield_one_word_rmw.cpp, tests/mmio/bitfield_bus_word_le_stream.cpp, tests/mmio/bitfield_fallback_byte_window.cpp, tests/mmio_xview/native_exact_uses_scalar_width.cpp, tests/mmio_xview/non_native_path_uses_bus_words.cpp, tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp, tests/mmio_xview/bitfield_fallback_byte_window.cpp, tests/mmio_xview/barrier_placement.cpp, tests/mmio_xview/align_unchecked_no_check.cpp, tests/mmio_xview/align_assert_checks.cpp, tests/mmio_xview/align_trap_traps.cpp, tests/mmio_xview/align_assume_is_ub_contract.cpp, tests/mmio_xview/static_validate_enforce_bus_basealign.cpp, tests/mmio_xview/strict_mmio_fallback_behavior.cpp, and tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


## 1. The MMIO surface area: `view` versus `xview`

Madpacket exposes two volatile view families under `mad::reg`.

The first family is `mad::reg::view<Packet, Bus, BaseAlign>` and `mad::reg::cview<Packet, Bus, BaseAlign>`. You construct these with `mad::reg::make_view(volatile void*)` or `mad::reg::make_view(volatile void const*)`. Conceptually, this family answers: “Given that I have a volatile base pointer, how do I read and write fields with the same naming and packing model as normal buffer views?”

The second family is `mad::reg::xview<Packet, Cfg>` and `mad::reg::xcview<Packet, Cfg>`. You construct these with `mad::reg::make_xview(volatile void*)` or `mad::reg::make_xview(volatile void const*)`. Conceptually, this family answers: “Given that I also know things about my bus (allowed access widths, alignment behavior, what should happen on misalignment), how do I force those constraints into the type system and into the access patterns?”

Both families expose essentially the same “layout-facing” API: `get<Name>()`, `set<Name>(value)`, and index-based variants. The difference is what the view is allowed to assume about alignment and what it is allowed to do about access width.

If you are reading this because you have a register map that says “these registers must be accessed with 32-bit transactions and will fault or corrupt on byte transactions”, you should treat `xview` as the baseline. The simple `view` family does not attempt to encode or enforce bus transaction width as a hard constraint. It is a volatile-friendly analogue of the normal buffer view, not a hardware policy engine.

Validated by tests/mmio/basic_scalar_endian.cpp and tests/mmio_xview/native_exact_uses_scalar_width.cpp.


## 2. Bus words, “LE stream numeric”, and the terminology used by the implementation

The MMIO code is built around a small “bus type” concept. A bus type is a compile-time descriptor that provides four facts.

It provides `Bus::word`, the C++ integral type the library treats as the bus’s natural word type, such as `std::uint32_t` for a 32-bit bus.

It provides `Bus::bytes`, the number of bytes in that word (1, 2, 4, or 8).

It provides `Bus::bits`, which is `Bus::bytes * 8`.

It provides `Bus::align`, which is the alignment requirement the library treats as “bus word alignment” for typed access.

The header provides concrete bus types like `mad::reg::bus8`, `mad::reg::bus16`, `mad::reg::bus32`, and `mad::reg::bus64`, each with the obvious parameters.

A second term that appears throughout the implementation is “LE stream numeric”. This is not a C++ endian tag. It is a local representation used for bitfields. When the library needs to extract or insert an arbitrary bit range within a bus word, it does that manipulation in a numeric space where “byte 0 in memory is the least significant byte of the number”, regardless of host endianness. That is exactly the same convention as the global semantics contract for normal packet views: it is the convention that makes bit numbering and bit slicing stable.

The consequence is that when the library loads a bus word as a host integer (for example, by doing a typed volatile load of `std::uint32_t`), it converts that host integer into LE stream numeric form before doing bitfield masks and shifts, and it converts back before storing.

Finally, the term “transaction width” in this doc is about the width of the volatile accesses the compiler emits. “Bus word size” is about a logical grouping of bytes in the algorithm. These are often the same, but they are not always the same, and strict MMIO mode explicitly makes them differ.

Validated by tests/mmio/bitfield_bus_word_le_stream.cpp and tests/mmio_xview/non_native_path_uses_bus_words.cpp.


## 3. What is a “transaction” in madpacket MMIO code

In this document, a “transaction” is an observable volatile access in the C++ abstract machine. This is intentionally defined in terms of C++ rather than in terms of any particular interconnect, because the library cannot promise what a compiler will lower a volatile access into on a specific target.

A typed volatile read or write, such as reading a `volatile std::uint32_t` or writing one, is one volatile access. On most embedded targets, that is also one bus transaction of that width, but the library does not promise that as a language-level guarantee.

A bytewise volatile read or write is a volatile access of a single byte. If the library reads four bytes via four volatile byte loads, it is doing four volatile transactions in this sense.

A read-modify-write sequence is a pattern: the library performs a volatile read of a containing region, modifies some bits/bytes in a temporary value, and then performs a volatile store of the containing region. The important property is that bits and bytes outside the written region are preserved exactly as they were observed by the read. This is a preservation guarantee, not an atomicity guarantee. If another bus master writes the same register between the read and the write, the library does not protect you from that race.

In MMIO programming, “transaction counting” often matters. You should interpret madpacket’s counting and policies as describing its own access algorithm. You should still verify, on your toolchain, what those volatile operations turn into.

Validated by tests/bitfields/rmw_preserves_neighbor_bits.cpp, tests/mmio/bitfield_one_word_rmw.cpp, and tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp.


## 4. The barrier hook: `MAD_MMIO_BARRIER()` and its placement rules

Madpacket uses a barrier hook named `MAD_MMIO_BARRIER()`.

If you do not provide this macro, the header defines it as empty. In that case, barriers do nothing and exist only as “places where a user barrier could be”.

If you do provide it, the MMIO layer invokes it at specific points. The library treats it as an opaque statement, and it does not attempt to reason about what the macro does.

The placement rules are deliberately simple and stable.

For scalar stores (byte-aligned 8/16/32/64 integer fields), the library inserts a barrier immediately before the volatile store sequence and immediately after the store sequence.

For bitfield stores that are implemented as an RMW of a containing region (bus word path), the library performs the volatile read, computes the merged value, then inserts a barrier, then performs the volatile store sequence, then inserts a barrier.

For bitfield stores that are implemented as a minimal byte window write, the library inserts a barrier immediately before calling the windowed write helper and immediately after it.

The library does not insert barriers around pure reads. If your platform requires an “I/O read barrier” before observing a device register value, you must express that yourself, either by explicitly invoking your barrier macro before the read, or by defining your barrier macro to include both compiler and hardware fence semantics that match your platform rules. The library intentionally does not guess.

Validated by tests/mmio/barrier_placement.cpp and tests/mmio_xview/barrier_placement.cpp.


## 5. `mad::reg::view`: construction-time checks and what they mean

A `mad::reg::view` binds to a `volatile std::byte*` base pointer. The recommended constructor is `mad::reg::make_view`, which takes a `volatile void*` or `volatile void const*` and returns a `view` or `cview`.

`make_view` performs a `MAD_ASSERT` that the base pointer is aligned to `BaseAlign`. This is a debug assertion, not an always-on runtime check. If assertions are disabled, this check disappears. The alignment becomes a precondition: your program must satisfy it.

The view then stores the base pointer and performs no further runtime checks on it.

The `BaseAlign` parameter is primarily used as an alignment bound for whether the implementation is allowed to attempt typed volatile accesses for scalar loads/stores. It is not, by itself, a bus-enforcement promise. A `view` can and will emit bytewise accesses in some paths even when `Bus` is wider, because the simple view’s job is correctness under C++ semantics, not hardware width enforcement.

Validated by tests/mmio/make_view_basealign_asserts.cpp.


## 6. `mad::reg::view`: byte-aligned scalar integer fields

For an integer field whose width is exactly 8, 16, 32, or 64 bits and whose bit offset is byte-aligned, `mad::reg::view` uses the scalar MMIO path.

The scalar path reads or writes exactly N bytes where N is 1, 2, 4, or 8. The user-visible `get` returns the field’s value as a 64-bit signed or unsigned integer (sign extension and masking behave exactly as in non-MMIO views), and endian tags on the field are applied as defined by the global semantics contract.

The scalar path chooses between two implementation strategies.

If strict MMIO mode is not enabled, the library may perform a typed volatile load/store of `std::uint{8,16,32,64}_t` (or a corresponding unsigned type) when it can prove the pointer alignment is sufficient. The alignment proof is conservative: the computed field address must be aligned to the type’s alignment, and the configured base alignment must be at least that alignment. If both are true, the library forms a typed volatile pointer and performs one volatile access of that type.

If strict MMIO mode is enabled (`MADPACKET_STRICT_MMIO`), the library intentionally does not form typed volatile pointers for MMIO regions. It implements scalar loads as sequences of volatile byte loads, assembled into a host integer in host memory order. It implements scalar stores as sequences of volatile byte stores that write the host integer’s byte representation to memory. Endian tags are still applied at the value level, which means the stored byte sequence still matches the declared endian tag; the only difference is that it takes multiple bytewise volatile operations to do so.

The key thing to internalize is this: `mad::reg::view` is allowed to be “transaction width ambiguous” under strict mode, because strict mode is a C++ correctness mode. If you need transaction width guarantees, you need `xview` plus appropriate macros, covered later.

Validated by tests/mmio/basic_scalar_endian.cpp and tests/mmio_xview/strict_mmio_fallback_behavior.cpp.


## 7. `mad::reg::view`: bitfields and other non-byte-multiple integer fields

For integer fields that are not whole bytes, `mad::reg::view` uses the bitfield MMIO path.

The first enforced rule is endian legality. A non-byte-multiple integer field is not allowed to carry an endian tag. `reg::view` enforces this by a compile-time `static_assert` that the field’s endian is native. This matches the global semantics contract: endian tags are only meaningful for byte sequences.

The second rule is how the library chooses between two extraction/update strategies.

If the field fits entirely within a single bus word, where “bus word” here means a contiguous region of `Bus::bytes` bytes starting at an address aligned to the bus word grid (word index times bus bytes), the library uses the “bus word mask” strategy. It reads those `Bus::bytes` bytes from the bus word region, interprets them as an LE stream numeric value, and extracts the bit range by mask-and-shift. For writes, it reads the bus word region, merges the new bit range, then writes the full bus word region back.

The important subtlety is the one that tends to surprise people: in the simple view, this bus word region access is implemented as bytewise volatile loads and bytewise volatile stores of those bytes. The helpers used are `mmio_load_u64_le_n<Bus::bytes>` and `mmio_store_u64_le_n<Bus::bytes>`, which explicitly iterate over bytes. Even when strict mode is not enabled, these helpers are bytewise. In other words, in the simple view, “bus word path” refers to the size of the region considered and the masking model, not to a guarantee of a single typed bus-word transaction.

If the field does not fit in a single bus word, the library falls back to the minimal byte window strategy. It reads the minimal byte window that covers the bitfield, performs the bit update in that temporary buffer, and writes that same minimal window back. This fallback is also bytewise by nature, and it can involve writes that touch multiple bus words.

The tests in `tests/mmio` validate semantic properties of these paths (bit order, preservation, fallback correctness), not hardware bus compliance.

Validated by tests/mmio/bitfield_bus_word_le_stream.cpp, tests/mmio/bitfield_one_word_rmw.cpp, and tests/mmio/bitfield_fallback_byte_window.cpp.


## 8. `mad::reg::view`: bytes fields and subpacket fields in MMIO

A bytes field in an MMIO view is deliberately not “copied” by the library. When you `get` a bytes field from a `reg::view` or `reg::cview`, you get back a small reference-like object that contains a `volatile std::byte*` (or `volatile std::byte const*` for const views) and a compile-time constant byte length. The library does not provide a `set` for bytes fields, because any attempt to do so would implicitly choose a transaction strategy that could be unsafe for devices with nontrivial semantics.

A subpacket field in an MMIO view is purely a pointer arithmetic composition. When you `get` a subpacket, the view computes the byte offset of the subpacket (which is always a whole number of bytes), adds that offset to the base pointer, and returns a new MMIO view bound to that derived pointer. There is no deep copy, no revalidation beyond what the nested view itself does, and no special casing beyond “this is just a new base pointer for the nested packet layout”.

As with normal views, attempting to `set` a subpacket field is ill-formed. A subpacket field is a view-producing field, not a value field.

Validated by tests/api/reject_set_on_bytes_compile_fail.cpp, tests/api/reject_set_on_subpacket_compile_fail.cpp, and tests/layout/subpacket_offsets_golden.cpp.


## 9. `mad::reg::xview`: configuration and construction rules

The `xview` family introduces a configuration type, `mad::reg::cfg`.

A configuration contains a bus type, a base alignment, a width policy, an alignment policy, and a capability descriptor (which itself carries read and write masks). The idea is that “policy decisions” become part of the type and can be validated at compile time.

The constructor helpers `make_xview` and `make_xview` for const pointers enforce base alignment according to the chosen alignment policy. They first enforce `Cfg::base_align` on the base pointer. Then, if the width policy is `enforce_bus`, they additionally enforce `Bus::align` on the base pointer. This extra check is a clear, direct meaning of “enforce bus”: if you cannot even guarantee bus alignment at the base, you cannot promise bus-word transactions at derived addresses either.

Unlike `make_view`, which uses `MAD_ASSERT`, `make_xview` can enforce misalignment in four different ways depending on `Cfg::align`. This is covered in the next section.

Validated by tests/mmio_xview/align_assert_checks.cpp and tests/mmio_xview/static_validate_enforce_bus_basealign.cpp.


## 10. `xview` alignment policies: unchecked, assert, trap, assume

`xview` uses an `align_policy` to decide what to do when alignment requirements are not met.

`unchecked` means the library performs no alignment checks. If you violate alignment requirements and your platform faults or produces undefined device behavior, that is entirely on you.

`assert_` means the library uses `MAD_ASSERT` for alignment checks. In debug builds, misalignment aborts. In release builds with assertions removed, the checks disappear and misalignment becomes a precondition.

`trap` means the library calls a “trap now” primitive when misalignment is detected. This is a fail-fast mode that remains active in release builds.

`assume` means the library emits an optimizer assumption that the pointer is aligned. If it is not, behavior is undefined. This policy is for performance-critical contexts where you would rather have UB than any runtime check or trap, and you are willing to treat alignment as part of the program’s proof obligations.

These policies are applied in `make_xview` for the base pointer, and they are also applied inside the bus-word helpers whenever the library performs a bus-word load or store. This second application matters because nested subpacket addresses and other derived addresses might have stricter alignment requirements than the base alignment.

Validated by tests/mmio_xview/align_unchecked_no_check.cpp, tests/mmio_xview/align_assert_checks.cpp, tests/mmio_xview/align_trap_traps.cpp, and tests/mmio_xview/align_assume_is_ub_contract.cpp.


## 11. `xview` width policies: what they mean and what the implementation actually does

The `xview` width policy is a compile-time enum `mad::reg::width_policy`. It exists to express “what access widths should be attempted for scalar fields” and to drive both compile-time validation and access pattern selection.

The policies are `native`, `enforce_bus`, `prefer_bus`, and `minimal_ok`.

The safest way to understand these, based on current implementation behavior, is to split scalar accesses into two conceptual categories.

The first category is the “native exact” category: a byte-aligned field whose width is exactly one of 1, 2, 4, or 8 bytes, and where the chosen policy and capability mask allow accessing that field with a field-sized volatile load/store. In that category, and only when the policy is actually `native`, the implementation uses `mmio_load_pod` and `mmio_store_pod` for the exact width. When strict mode is off and alignment permits, these can compile down to one typed volatile transaction of that width.

The second category is the “bus-word fallback” category: everything else, including all non-native policies, and including cases where the capability masks do not allow the exact width. In that category, the implementation performs bus-word based loads and stores and assembles/scatters the field’s bytes from those bus words. This is the path that inherently supports RMW preservation and spanning behavior.

This has two immediate consequences.

First, if you choose `enforce_bus`, you are promising that all scalar accesses will be handled in the bus-word fallback category, even if the scalar field’s width equals 1, 2, or 4 bytes. That promise is what you want for hardware that forbids narrow accesses, and it is what the test suite refers to as “uses bus words”.

Second, if you choose `prefer_bus` or `minimal_ok`, you should not interpret them as “the library will always pick the minimally sufficient typed width on the hardware”. In the current implementation, these policies influence width selection and RMW decision logic, but the fallback category still operates in bus-word units. They are therefore most meaningful when you select a bus type that matches your intended minimal width (for example, bus16 if you truly want 16-bit words), or when you treat the policy as declarative documentation plus compile-time accounting rather than as a guarantee of emitting narrower typed volatile accesses.

Validated by tests/mmio_xview/native_exact_uses_scalar_width.cpp and tests/mmio_xview/non_native_path_uses_bus_words.cpp.


## 12. The bus-word helpers in `xview`, and why “LE stream numeric” is explicit here

The policy view performs bus-word loads and stores through helper functions that are distinct from the simple view’s bytewise “load N bytes as LE stream numeric” helpers.

The `xview` bus-word helpers come in two layers.

The first layer loads and stores a bus word as a host integer. This layer is responsible for alignment enforcement. In non-strict mode, when alignment permits, it will do a typed volatile access of `Bus::word`. When alignment does not permit, or when strict mode is enabled, it falls back to a bytewise `mmio_load_pod` / `mmio_store_pod` implementation. The key property is that this helper returns a host integer numeric value for the bus word’s bytes in memory order on this host.

The second layer loads and stores a bus word as an LE stream numeric value. This layer exists because bitfield extraction and insertion must be done in LE stream numeric space, not in host-endian integer space. The helper therefore either (in strict mode) uses a bytewise “LE stream numeric” load that already yields LE stream numeric, or (in non-strict mode) converts the host integer into LE stream numeric using a conversion function that depends on host endianness.

This is why `xview` bitfield code can correctly manipulate bit ranges on big-endian hosts without having to special-case the host endianness at every call site.

Validated by tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp and tests/mmio/bitfield_bus_word_le_stream.cpp.


## 13. Scalar bus-word fallback details: how many words are touched, and when RMW happens

In the bus-word fallback category, scalar loads and stores are implemented in terms of bus words.

A scalar load of N bytes at byte offset O is implemented as one of the following cases.

If N equals the bus width B, it is a single bus-word load.

If N is smaller than B and the region [O, O+N) lies entirely within one bus word, it is a single bus-word load plus extraction of the corresponding byte region to produce a native value.

If N is smaller than B and the region spans two bus words, it is two bus-word loads and a gather of the bytes that cross the boundary.

If N is larger than B, it is a sequence of bus-word loads sufficient to cover N bytes, plus assembly of those bytes into the native value.

A scalar store of N bytes at byte offset O is similar but adds the concept of RMW preservation.

If N equals B, it is a single bus-word store of the host integer value that corresponds to the field’s bytes, and no RMW is required.

If N is smaller than B and the region lies within one bus word, the store helper decides whether to RMW. When RMW is required, it performs one bus-word load, merges the updated byte region into that word while preserving untouched bytes, and then stores the full bus word. When RMW is not required, it is allowed to synthesize the full word from the field bytes and store it directly.

If N is smaller than B and the region spans two bus words, the store helper performs per-word updates. In RMW mode, it loads both affected bus words, merges the relevant bytes, and stores both words. Without RMW, it may synthesize words and store them, but in practice most register programming models treat cross-word stores as inherently risky, so you should not rely on non-RMW in this shape unless you know your device semantics.

If N is larger than B, the store helper performs one bus-word store per bus word. If N is not an exact multiple of B, the final partial word update may require RMW to preserve the tail bytes of that final word, depending on policy.

The RMW decision logic in the current implementation is conservative. It treats “writing wider than the field” as requiring RMW, and it treats “writing a field whose size is not a multiple of the bus width” as requiring RMW for the final partial word. This matches the intuitive “do not clobber neighbors” rule.

Validated by tests/mmio_xview/non_native_path_uses_bus_words.cpp and tests/bitfields/rmw_preserves_neighbor_bits.cpp.


## 14. Bitfields in `xview`: bus-word RMW when possible, minimal window fallback otherwise

The `xview` bitfield model is aligned with the simple view’s model but is stricter about what “bus word” means when bus-word helpers are available.

If the bitfield fits in one bus word, `xview` reads that bus word via the bus helper, converts to LE stream numeric, extracts the bit range, and returns it with sign extension or zero extension as appropriate. For writes, it loads the bus word, merges the updated bit range in LE stream numeric space, converts back to host integer space if necessary, and stores the bus word back. This is a classic bus-word RMW update.

If the bitfield does not fit in one bus word, `xview` falls back to the minimal byte window algorithm and uses bytewise read/write helpers that operate on exactly the needed bytes. The store path is still bracketed by barriers.

This is the place where you must not over-interpret `enforce_bus` as “all accesses are bus-word sized”. `enforce_bus` forces scalar fields into bus-word access patterns, and it influences which helper paths are used, but a bitfield that fundamentally spans multiple bus words cannot be updated with a single bus-word RMW by definition. The fallback window path exists for correctness, and your job as a hardware programmer is to avoid declaring such fields when your hardware forbids cross-word access sequences.

Validated by tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp and tests/mmio_xview/bitfield_fallback_byte_window.cpp.


## 15. Capability masks and “forbidden widths”: what you can rely on

The capability descriptor in `xview` carries two width masks: one for reads and one for writes. A width mask is a declarative statement about which transaction widths are legal on your hardware.

In the current implementation, these masks are used to decide whether the “native exact” path is allowed. If the mask includes the field’s width and the policy is `native`, the native exact path can be taken. If it does not, the implementation promotes to the bus-word fallback category.

The implementation does not currently perform a universal runtime check that “the bus-word fallback is actually allowed by the mask”. The intended usage is that your configuration is internally consistent: if you set Bus to bus32, your masks must allow 4-byte operations, because the fallback category fundamentally uses bus words.

The phrase “forbidden width” therefore has two concrete, enforceable meanings in this library today.

A width is forbidden by mask for the native exact path, which forces promotion.

A width is forbidden by strict MMIO mode combined with hard width enforcement, which can cause compilation to fail rather than silently degrading to bytewise operations.

If you are on a target where forbidden widths are a correctness concern rather than a preference concern, you should pair `xview` with an explicit bus type that matches the only allowed width and with masks that allow only that width, and you should understand the strict-mode interaction described next.

Validated by tests/mmio_xview/native_exact_uses_scalar_width.cpp and tests/mmio_xview/strict_mmio_fallback_behavior.cpp.


## 16. Strict MMIO mode and hard width enforcement: the two macros and their joint meaning

`MADPACKET_STRICT_MMIO` is a build macro that changes how the library performs volatile accesses. When it is enabled, typed volatile loads and stores are disabled in favor of bytewise volatile assembly/disassembly. This is a “C++ conservatism” mode: it avoids forming typed volatile pointers for memory that is not actually an object of that type, and it avoids relying on compiler behavior around volatile typed access.

This mode has an unavoidable implication: if a device requires bus-word transactions and forbids byte transactions, strict mode will tend to violate that requirement by emitting bytewise operations in helper paths.

That is why `MADPACKET_MMIO_HARDWIDTH` exists. When hard width enforcement is enabled in strict mode, the library refuses to instantiate certain helper paths that would otherwise degrade to bytewise behavior. Instead, those paths become compile-time errors. This forces you to decide, explicitly, whether you want strict C++ behavior or strict hardware width behavior.

The test suite treats this as a non-negotiable property: strict mode alone should have predictable fallback behavior, and strict mode plus hard width should refuse to compile rather than silently changing transaction width.

Validated by tests/mmio_xview/strict_mmio_fallback_behavior.cpp and tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp.


## 17. A test-anchor map for MMIO and xview

The MMIO behavior is intentionally grounded by tests whose names reflect the specific regression risks.

The core `reg::view` MMIO tests live under `tests/mmio`.

`tests/mmio/basic_scalar_endian.cpp` exists to validate that endian tags on scalar fields behave identically in MMIO views and non-MMIO views, except that the backing storage is accessed via volatile operations.

`tests/mmio/barrier_placement.cpp` exists to validate the placement of `MAD_MMIO_BARRIER()` around store sequences in the simple view.

`tests/mmio/bitfield_bus_word_le_stream.cpp` exists to validate that the “fits in one word” bitfield path interprets the containing word as an LE byte stream numeric and therefore extracts the expected values independent of host endianness.

`tests/mmio/bitfield_one_word_rmw.cpp` exists to validate preservation of neighboring bits when updating a bitfield that fits in one word.

`tests/mmio/bitfield_fallback_byte_window.cpp` exists to validate correctness of the fallback minimal window algorithm for bitfields that do not fit in one word.

The policy view tests live under `tests/mmio_xview`.

`tests/mmio_xview/native_exact_uses_scalar_width.cpp` exists to validate that the native width policy uses field-sized scalar access when it is permitted.

`tests/mmio_xview/non_native_path_uses_bus_words.cpp` exists to validate that non-native paths use bus-word based access patterns rather than field-sized access patterns.

`tests/mmio_xview/bitfield_one_word_is_bus_rmw.cpp` exists to validate that one-word bitfields use bus-word RMW updates rather than byte-window updates.

`tests/mmio_xview/bitfield_fallback_byte_window.cpp` exists to validate the fallback window behavior for bitfields in the policy view.

`tests/mmio_xview/barrier_placement.cpp` exists to validate barrier placement in the policy view.

`tests/mmio_xview/align_unchecked_no_check.cpp`, `tests/mmio_xview/align_assert_checks.cpp`, `tests/mmio_xview/align_trap_traps.cpp`, and `tests/mmio_xview/align_assume_is_ub_contract.cpp` exist to validate each alignment policy.

`tests/mmio_xview/static_validate_enforce_bus_basealign.cpp` exists to validate the compile-time rejection rule that `enforce_bus` requires `base_align` to be at least the bus alignment.

`tests/mmio_xview/strict_mmio_fallback_behavior.cpp` exists to validate strict mode behavior.

`tests/mmio_xview/hardwidth_rejects_strict_mode_compile_fail.cpp` exists to validate hardwidth behavior in strict mode.

Validated by the full list of MMIO and MMIO xview tests named in this section.
