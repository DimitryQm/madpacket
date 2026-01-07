#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <bit>
#include <utility>

#include "madpacket.hpp"

namespace mad_test::ref {
  // helpers (independent of lib internals)
  static inline constexpr std::uint64_t mask64(std::size_t bits) noexcept {
    return (bits >= 64) ? ~0ull : ((1ull << bits) - 1ull);
  }

  static inline constexpr bool host_is_little() noexcept {
    return std::endian::native == std::endian::little;
  }

  enum class endian_mode : std::uint8_t { native, little, big };

  template <typename Tag>
  static inline consteval endian_mode tag_mode() {
    if constexpr (std::is_same_v<Tag, mad::native_endian_t>) return endian_mode::native;
    if constexpr (std::is_same_v<Tag, mad::little_endian_t>) return endian_mode::little;
    if constexpr (std::is_same_v<Tag, mad::big_endian_t>)    return endian_mode::big;
    else return endian_mode::native;
  }

  static inline std::uint64_t load_bytes_as_u64(const std::byte* p, std::size_t n, endian_mode m) noexcept {
    // Reads exactly n bytes (n<=8), returns numeric value.
    // This is a reference implementation: bytewise, no type-punning, no memcpy tricks.
    std::uint64_t v = 0;

    const bool le =
      (m == endian_mode::little) ? true :
      (m == endian_mode::big)    ? false :
      host_is_little();

    if (le) {
      for (std::size_t i = 0; i < n; ++i) {
        v |= (std::uint64_t(std::to_integer<unsigned char>(p[i])) << (8u * i));
      }
    } else {
      for (std::size_t i = 0; i < n; ++i) {
        v = (v << 8) | std::uint64_t(std::to_integer<unsigned char>(p[i]));
      }
    }
    return v;
  }

  static inline void store_u64_as_bytes(std::byte* p, std::size_t n, endian_mode m, std::uint64_t v) noexcept {
    // Stores exactly n bytes (n<=8) in the requested byte order.
    const bool le =
      (m == endian_mode::little) ? true :
      (m == endian_mode::big)    ? false :
      host_is_little();

    if (le) {
      for (std::size_t i = 0; i < n; ++i) {
        p[i] = std::byte((v >> (8u * i)) & 0xFFu);
      }
    } else {
      for (std::size_t i = 0; i < n; ++i) {
        const std::size_t shift = 8u * (n - 1u - i);
        p[i] = std::byte((v >> shift) & 0xFFu);
      }
    }
  }

  // Bit numbering: bit0 = LSB of byte0, then upwards, then next byte.
  static inline bool read_bit_le_stream(const std::byte* base, std::size_t bit_index) noexcept {
    const std::size_t byte = bit_index >> 3;
    const std::size_t bit  = bit_index & 7u; // 0 = LSB
    const unsigned b = std::to_integer<unsigned char>(base[byte]);
    return ((b >> bit) & 1u) != 0u;
  }

  static inline void write_bit_le_stream(std::byte* base, std::size_t bit_index, bool value) noexcept {
    const std::size_t byte = bit_index >> 3;
    const std::size_t bit  = bit_index & 7u; // 0 = LSB
    unsigned b = std::to_integer<unsigned char>(base[byte]);
    const unsigned mask = (1u << bit);
    b = value ? (b | mask) : (b & ~mask);
    base[byte] = std::byte(b);
  }

  static inline std::uint64_t read_bits_le_stream(const std::byte* base, std::size_t bit_off, std::size_t bits) noexcept {
    // Very explicit: O(bits). Perfect for a reference model.
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < bits; ++i) {
      if (read_bit_le_stream(base, bit_off + i)) {
        out |= (1ull << i);
      }
    }
    return out;
  }

  static inline void write_bits_le_stream(std::byte* base, std::size_t bit_off, std::size_t bits, std::uint64_t value) noexcept {
    // Only affects [bit_off, bit_off+bits); preserves all other bits.
    for (std::size_t i = 0; i < bits; ++i) {
      const bool bit = ((value >> i) & 1ull) != 0ull;
      write_bit_le_stream(base, bit_off + i, bit);
    }
  }

  static inline std::int64_t sign_extend(std::uint64_t x, std::size_t bits) noexcept {
    // Two's complement sign-extend from 'bits' (1..64)
    if (bits >= 64) return static_cast<std::int64_t>(x);
    const std::uint64_t sign = 1ull << (bits - 1u);
    const std::uint64_t ext  = (x ^ sign) - sign;
    return static_cast<std::int64_t>(ext);
  }
  // Packet-level reference accessors
  template <typename Packet, mad::fixed_string Name>
  struct field {
    static constexpr std::size_t idx = Packet::template index_of<Name>;
    static_assert(idx != static_cast<std::size_t>(-1), "ref::field: name not found in packet");
    using type = typename Packet::template field_t<idx>;
    static constexpr std::size_t bit_off  = Packet::offsets_bits[idx];
    static constexpr std::size_t bit_size = Packet::sizes_bits[idx];
    static constexpr std::size_t byte_off = bit_off >> 3;
    static constexpr std::size_t shift    = bit_off & 7u;
  };

  template <typename Packet, mad::fixed_string Name>
  static inline auto get(const std::byte* base) noexcept {
    using F = typename field<Packet, Name>::type;
    constexpr std::size_t bit_off  = field<Packet, Name>::bit_off;
    constexpr std::size_t bits     = field<Packet, Name>::bit_size;
    constexpr std::size_t shift    = field<Packet, Name>::shift;
    constexpr std::size_t byte_off = field<Packet, Name>::byte_off;

    if constexpr (F::kind == mad::field_kind::int_bits) {
      // "byte-aligned scalar integer field" in implementation sense:
      if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
        const std::size_t nbytes = bits / 8;
        constexpr endian_mode em = tag_mode<typename F::endian>();
        std::uint64_t x = load_bytes_as_u64(base + byte_off, nbytes, em) & mask64(bits);

        if constexpr (F::is_signed) {
          return static_cast<std::int64_t>(static_cast<std::int64_t>(static_cast<std::int64_t>(x)));
        } else {
          return static_cast<std::uint64_t>(x);
        }
      } else {
        // Bitfield path: always le-stream numeric
        std::uint64_t x = read_bits_le_stream(base, bit_off, bits) & mask64(bits);
        if constexpr (F::is_signed) return sign_extend(x, bits);
        else return x;
      }
    } else if constexpr (F::kind == mad::field_kind::bytes) {
      // byte alignment is a precondition in the contract
      return base + byte_off;
    } else if constexpr (F::kind == mad::field_kind::subpacket) {
      return base + byte_off;
    } else {
      // pad fields are not addressable
      struct pad_not_addressable {};
      return pad_not_addressable{};
    }
  }

  template <typename Packet, mad::fixed_string Name, typename V>
  static inline void set(std::byte* base, V v) noexcept {
    using F = typename field<Packet, Name>::type;
    constexpr std::size_t bit_off  = field<Packet, Name>::bit_off;
    constexpr std::size_t bits     = field<Packet, Name>::bit_size;
    constexpr std::size_t shift    = field<Packet, Name>::shift;
    constexpr std::size_t byte_off = field<Packet, Name>::byte_off;

    if constexpr (F::kind == mad::field_kind::int_bits) {
      const std::uint64_t in = static_cast<std::uint64_t>(v);
      const std::uint64_t truncated = in & mask64(bits);

      if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
        const std::size_t nbytes = bits / 8;
        constexpr endian_mode em = tag_mode<typename F::endian>();
        store_u64_as_bytes(base + byte_off, nbytes, em, truncated);
      } else {
        write_bits_le_stream(base, bit_off, bits, truncated);
      }
    } else {
      // bytes/subpacket/pad: set not supported in contract
    }
  }

} // namespace mad_test::ref
