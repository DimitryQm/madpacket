#pragma once
/*
  madpacket.hpp - single-header, zero-copy, compile-time packet layout + view.

  Goal: Define binary packet formats as C++ types (with named fields), and get/set fields
  in raw byte buffers with fully compile-time computed offsets, sizes, endianness, and
  bit packing. Designed for embedded/network/protocol work where "struct overlay" is
  unsafe (endianness, padding, UB) and runtime serializers are too slow.

  C++20 required (class NTTP for field names).

  SPDX-License-Identifier: MIT
*/
#if __cplusplus < 202002L
#  error "madpacket requires C++20"
#endif
#ifndef MADPACKET_HPP_INCLUDED
#define MADPACKET_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <array>
#include <tuple>
#include <bit>
#include <limits>

#if __has_include(<string_view>)
  #include <string_view>
#endif

#if __has_include(<span>)
  #include <span>
#endif

#if defined(_MSC_VER)
  #include <intrin.h>
  #define MAD_FORCEINLINE __forceinline
  #define MAD_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
  #define MAD_FORCEINLINE __attribute__((always_inline)) inline
  #define MAD_NOINLINE __attribute__((noinline))
#else
  #define MAD_FORCEINLINE inline
  #define MAD_NOINLINE
#endif
#if defined(__clang__)
  #define MAD_UNROLL _Pragma("unroll")
#elif defined(__GNUC__)
  #define MAD_UNROLL _Pragma("GCC unroll 16")
#else
  #define MAD_UNROLL
#endif

#ifndef MAD_ASSERT
  #include <cassert>
  #define MAD_ASSERT(x) assert(x)
#endif

namespace mad {

   // fixed_string (NTTP names)
   
  template <std::size_t N>
  struct fixed_string {
    char v[N]{};
    constexpr fixed_string(char const (&s)[N]) noexcept {
      for (std::size_t i = 0; i < N; ++i) v[i] = s[i];
    }
    constexpr char const* c_str() const noexcept { return v; }
    static constexpr std::size_t size() noexcept { return N; } // includes '\0'
    constexpr char operator[](std::size_t i) const noexcept { return v[i]; }
    constexpr std::size_t len() const noexcept { return N ? (N - 1) : 0; }
  };

  template <std::size_t N1, std::size_t N2>
  constexpr bool operator==(fixed_string<N1> const& a, fixed_string<N2> const& b) noexcept {
    if constexpr (N1 != N2) return false;
    for (std::size_t i = 0; i < N1; ++i) if (a.v[i] != b.v[i]) return false;
    return true;
  }

  template <std::size_t N1, std::size_t N2>
  constexpr bool operator!=(fixed_string<N1> const& a, fixed_string<N2> const& b) noexcept {
    return !(a == b);
  }

  //  
  // endian tags
  //  
  struct little_endian_t {};
  struct big_endian_t {};
  struct native_endian_t {};

  inline constexpr little_endian_t little_endian{};
  inline constexpr big_endian_t big_endian{};
  inline constexpr native_endian_t native_endian{};

   
  // internal utilities
   
  namespace detail {

    template <typename T>
    using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

    template <std::size_t Bits>
    struct uint_for_bits {
      static_assert(Bits >= 1 && Bits <= 64, "integer bit width must be 1..64");
      using type =
        std::conditional_t<(Bits <= 8),  std::uint8_t,
        std::conditional_t<(Bits <= 16), std::uint16_t,
        std::conditional_t<(Bits <= 32), std::uint32_t,
                           std::uint64_t>>>;
    };
    template <std::size_t Bits>
    using uint_for_bits_t = typename uint_for_bits<Bits>::type;

    template <std::size_t Bits>
    struct int_for_bits {
      static_assert(Bits >= 1 && Bits <= 64, "integer bit width must be 1..64");
      using type =
        std::conditional_t<(Bits <= 8),  std::int8_t,
        std::conditional_t<(Bits <= 16), std::int16_t,
        std::conditional_t<(Bits <= 32), std::int32_t,
                           std::int64_t>>>;
    };
    template <std::size_t Bits>
    using int_for_bits_t = typename int_for_bits<Bits>::type;

    template <typename U>
    MAD_FORCEINLINE constexpr U bswap(U x) noexcept {
      if constexpr (sizeof(U) == 1) {
        return x;
      } else if constexpr (sizeof(U) == 2) {
        #if defined(_MSC_VER)
          return static_cast<U>(_byteswap_ushort(static_cast<unsigned short>(x)));
        #elif defined(__GNUC__) || defined(__clang__)
          return static_cast<U>(__builtin_bswap16(static_cast<std::uint16_t>(x)));
        #else
          return static_cast<U>(((x & 0x00ffu) << 8) | ((x & 0xff00u) >> 8));
        #endif
      } else if constexpr (sizeof(U) == 4) {
        #if defined(_MSC_VER)
          return static_cast<U>(_byteswap_ulong(static_cast<unsigned long>(x)));
        #elif defined(__GNUC__) || defined(__clang__)
          return static_cast<U>(__builtin_bswap32(static_cast<std::uint32_t>(x)));
        #else
          std::uint32_t y = static_cast<std::uint32_t>(x);
          y = (y >> 24) | ((y >> 8) & 0x0000FF00u) | ((y << 8) & 0x00FF0000u) | (y << 24);
          return static_cast<U>(y);
        #endif
      } else if constexpr (sizeof(U) == 8) {
        #if defined(_MSC_VER)
          return static_cast<U>(_byteswap_uint64(static_cast<unsigned long long>(x)));
        #elif defined(__GNUC__) || defined(__clang__)
          return static_cast<U>(__builtin_bswap64(static_cast<std::uint64_t>(x)));
        #else
          std::uint64_t y = static_cast<std::uint64_t>(x);
          y = (y >> 56) |
              ((y >> 40) & 0x000000000000FF00ull) |
              ((y >> 24) & 0x0000000000FF0000ull) |
              ((y >> 8)  & 0x00000000FF000000ull) |
              ((y << 8)  & 0x000000FF00000000ull) |
              ((y << 24) & 0x0000FF0000000000ull) |
              ((y << 40) & 0x00FF000000000000ull) |
              (y << 56);
          return static_cast<U>(y);
        #endif
      } else {
        static_assert(sizeof(U) == 1 || sizeof(U) == 2 || sizeof(U) == 4 || sizeof(U) == 8, "unsupported bswap size");
        return x;
      }
    }

    template <typename EndianTag>
    inline constexpr bool need_bswap =
      (std::endian::native == std::endian::little)
        ? std::is_same_v<EndianTag, big_endian_t>
        : std::is_same_v<EndianTag, little_endian_t>;

    template <typename EndianTag>
    inline constexpr bool is_native_endian =
      std::is_same_v<EndianTag, native_endian_t> ||
      ((std::endian::native == std::endian::little) && std::is_same_v<EndianTag, little_endian_t>) ||
      ((std::endian::native == std::endian::big) && std::is_same_v<EndianTag, big_endian_t>);

    template <std::size_t Bits>
    inline constexpr std::uint64_t mask64 = (Bits == 64) ? ~0ull : ((1ull << Bits) - 1ull);

    template <std::size_t N>
    MAD_FORCEINLINE constexpr std::uint64_t load_u64_le_n(std::byte const* p) noexcept {
      // safe: only reads N bytes (N <= 8), assembling little-endian
      std::uint64_t x = 0;
      MAD_UNROLL
      for (std::size_t i = 0; i < N; ++i) {
        x |= (static_cast<std::uint64_t>(std::to_integer<unsigned char>(p[i])) << (i * 8));
      }
      return x;
    }

#if defined(__SIZEOF_INT128__)
    template <std::size_t N>
    MAD_FORCEINLINE constexpr __uint128_t load_u128_le_n(std::byte const* p) noexcept {
      static_assert(N <= 16);
      __uint128_t x = 0;
      MAD_UNROLL
      for (std::size_t i = 0; i < N; ++i) {
        x |= (static_cast<__uint128_t>(std::to_integer<unsigned char>(p[i])) << (i * 8));
      }
      return x;
    }
#endif

    template <std::size_t BitOffset, std::size_t BitCount>
    struct bit_window {
      static constexpr std::size_t byte = BitOffset >> 3;
      static constexpr std::size_t shift = BitOffset & 7u;
      static constexpr std::size_t need_bytes = (shift + BitCount + 7u) >> 3; // <= 9 for BitCount<=64
      static_assert(BitCount >= 1 && BitCount <= 64);
      static_assert(need_bytes >= 1 && need_bytes <= 9);
    };

    template <std::size_t BitOffset, std::size_t BitCount>
    MAD_FORCEINLINE constexpr std::uint64_t read_bits_le(std::byte const* buf) noexcept {
      using W = bit_window<BitOffset, BitCount>;
#if defined(__SIZEOF_INT128__)
      __uint128_t raw = load_u128_le_n<W::need_bytes>(buf + W::byte);
      raw >>= W::shift;
      return static_cast<std::uint64_t>(raw) & mask64<BitCount>;
#else
      // fallback: assemble up to 9 bytes into two u64
      std::uint64_t lo = load_u64_le_n<(W::need_bytes <= 8 ? W::need_bytes : 8)>(buf + W::byte);
      std::uint64_t hi = 0;
      if constexpr (W::need_bytes > 8) {
        hi = static_cast<std::uint64_t>(std::to_integer<unsigned char>(buf[W::byte + 8]));
      }
      // combine into 72-bit stream: hi is byte 8
      // shift within 0..7
      std::uint64_t out;
      if constexpr (W::shift == 0) {
        out = lo;
      } else {
        out = (lo >> W::shift) | (hi << (64 - W::shift));
      }
      return out & mask64<BitCount>;
#endif
    }

    template <std::size_t BitOffset, std::size_t BitCount>
    MAD_FORCEINLINE constexpr void write_bits_le(std::byte* buf, std::uint64_t value) noexcept {
      using W = bit_window<BitOffset, BitCount>;
      value &= mask64<BitCount>;
#if defined(__SIZEOF_INT128__)
      __uint128_t raw = load_u128_le_n<W::need_bytes>(buf + W::byte);
      const __uint128_t m = (static_cast<__uint128_t>(mask64<BitCount>) << W::shift);
      raw = (raw & ~m) | (static_cast<__uint128_t>(value) << W::shift);
      MAD_UNROLL
      for (std::size_t i = 0; i < W::need_bytes; ++i) {
        buf[W::byte + i] = static_cast<std::byte>((raw >> (i * 8)) & 0xFFu);
      }
#else
      // manual: read bytes, modify in-place
      std::uint64_t lo = load_u64_le_n<(W::need_bytes <= 8 ? W::need_bytes : 8)>(buf + W::byte);
      std::uint64_t hi = 0;
      if constexpr (W::need_bytes > 8) {
        hi = static_cast<std::uint64_t>(std::to_integer<unsigned char>(buf[W::byte + 8]));
      }

      // build 72-bit in (hi:8bits, lo:64bits) representing bytes [0..need_bytes)
      // clear mask window then set
      // operate on lo/hi via shifts; BitCount<=64, shift<=7
      const std::uint64_t m = mask64<BitCount>;
      // We'll modify in a 128 emulation
      // Compute full 128 as two parts: low 64 bits are lo, high bits (0..8) are hi.
      // We need to clear m<<shift in low+high.
      const std::size_t s = W::shift;
      // clear in lo
      lo &= ~(m << s);
      lo |= (value << s);

      if constexpr (W::need_bytes > 8) {
        // bits spilling into hi are those above bit 63
        // When s != 0, value may set bits into hi via carry
        const std::uint64_t spill_mask = (m >> (64 - s)); // bits that go into hi
        const std::uint64_t spill_val  = (value >> (64 - s));
        // clear relevant bits in hi (hi is only 8 bits, but treat as u64)
        hi &= ~spill_mask;
        hi |= spill_val;
      }

      // store back bytes
      const std::size_t store_lo = (W::need_bytes <= 8 ? W::need_bytes : 8);
      MAD_UNROLL
      for (std::size_t i = 0; i < store_lo; ++i) {
        buf[W::byte + i] = static_cast<std::byte>((lo >> (i * 8)) & 0xFFu);
      }
      if constexpr (W::need_bytes > 8) {
        buf[W::byte + 8] = static_cast<std::byte>(hi & 0xFFu);
      }
#endif
    }

    template <std::size_t Bits, bool Signed>
    MAD_FORCEINLINE constexpr auto sign_extend(std::uint64_t x) noexcept {
      if constexpr (!Signed) {
        return x;
      } else {
        if constexpr (Bits == 64) {
          return static_cast<std::int64_t>(x);
        } else {
          const std::uint64_t sign = 1ull << (Bits - 1);
          const std::uint64_t extended = (x ^ sign) - sign;
          return static_cast<std::int64_t>(extended);
        }
      }
    }

    template <typename T>
    MAD_FORCEINLINE constexpr std::uint64_t to_u64(T v) noexcept {
      if constexpr (std::is_same_v<T, bool>) return v ? 1u : 0u;
      else if constexpr (std::is_enum_v<T>) return static_cast<std::uint64_t>(static_cast<std::underlying_type_t<T>>(v));
      else if constexpr (std::is_integral_v<T>) return static_cast<std::uint64_t>(v);
      else return static_cast<std::uint64_t>(v);
    }

    template <typename U>
    MAD_FORCEINLINE constexpr U load_pod_le(std::byte const* p) noexcept {
      U x{};
      std::memcpy(&x, p, sizeof(U));
      return x;
    }

    template <typename U>
    MAD_FORCEINLINE constexpr void store_pod_le(std::byte* p, U x) noexcept {
      std::memcpy(p, &x, sizeof(U));
    }

    template <typename...>
    struct type_list {};

    template <typename T, typename... Ts>
    struct index_of_type;
    template <typename T>
    struct index_of_type<T> : std::integral_constant<std::size_t, static_cast<std::size_t>(-1)> {};
    template <typename T, typename U, typename... Ts>
    struct index_of_type<T, U, Ts...> : std::integral_constant<std::size_t,
      std::is_same_v<T, U> ? 0 : (index_of_type<T, Ts...>::value == static_cast<std::size_t>(-1) ? static_cast<std::size_t>(-1) : 1 + index_of_type<T, Ts...>::value)
    > {};

    template <fixed_string Name, typename... Fields>
    struct find_field_index;
    template <fixed_string Name>
    struct find_field_index<Name> : std::integral_constant<std::size_t, static_cast<std::size_t>(-1)> {};
    template <fixed_string Name, typename F, typename... Rest>
    struct find_field_index<Name, F, Rest...> : std::integral_constant<std::size_t,
      (F::has_name && (F::name == Name)) ? 0 :
        (find_field_index<Name, Rest...>::value == static_cast<std::size_t>(-1) ? static_cast<std::size_t>(-1)
                                                                                : 1 + find_field_index<Name, Rest...>::value)
    > {};

    template <fixed_string Name, typename... Fields>
    inline constexpr std::size_t find_field_index_v = find_field_index<Name, Fields...>::value;

    template <fixed_string Name, typename... Fields>
    struct count_name;
    template <fixed_string Name>
    struct count_name<Name> : std::integral_constant<std::size_t, 0> {};
    template <fixed_string Name, typename F, typename... Rest>
    struct count_name<Name, F, Rest...> : std::integral_constant<std::size_t,
      (F::has_name && (F::name == Name) ? 1 : 0) + count_name<Name, Rest...>::value
    > {};

    template <fixed_string Name, typename... Fields>
    inline constexpr std::size_t count_name_v = count_name<Name, Fields...>::value;

    template <typename...>
    struct all_unique_names;

    template <>
    struct all_unique_names<> : std::true_type {};

    template <typename F, typename... Rest>
    struct all_unique_names<F, Rest...> : std::bool_constant<
      (!F::has_name || (count_name_v<F::name, Rest...> == 0)) && all_unique_names<Rest...>::value
    > {};
  } // namespace detail

   
  // Field definitions
   

  enum class field_kind : std::uint8_t { int_bits, bytes, pad, subpacket };

  template <fixed_string Name, std::size_t Bits, bool Signed, typename EndianTag = native_endian_t>
  struct int_field {
    static constexpr field_kind kind = field_kind::int_bits;
    static constexpr bool has_name = true;
    static constexpr auto name = Name;
    static constexpr std::size_t bits = Bits;
    static constexpr bool is_signed = Signed;
    using endian = EndianTag;

    static_assert(Bits >= 1 && Bits <= 64, "int_field Bits must be 1..64");
    static_assert((Bits % 8 == 0) || detail::is_native_endian<EndianTag>, "endianness is only meaningful for byte-multiple fields");
  };

  template <fixed_string Name, std::size_t N>
  struct bytes_field {
    static constexpr field_kind kind = field_kind::bytes;
    static constexpr bool has_name = true;
    static constexpr auto name = Name;
    static constexpr std::size_t bits = N * 8;
    static constexpr std::size_t bytes = N;
  };

  template <std::size_t Bits>
  struct pad_bits {
    static constexpr field_kind kind = field_kind::pad;
    static constexpr bool has_name = false;
    static constexpr auto name = fixed_string<1>{""};
    static constexpr std::size_t bits = Bits;
    static_assert(Bits >= 1, "pad_bits Bits must be >= 1");
  };

  template <std::size_t N>
  struct pad_bytes : pad_bits<N * 8> {};

  template <typename Packet, fixed_string Name>
  struct subpacket_field {
    static constexpr field_kind kind = field_kind::subpacket;
    static constexpr bool has_name = true;
    static constexpr auto name = Name;
    using packet = Packet;
    static constexpr std::size_t bits = Packet::total_bits;
  };

  // Friendly aliases
  template <fixed_string Name> using u1  = int_field<Name, 1,  false, native_endian_t>;
  template <fixed_string Name> using u2  = int_field<Name, 2,  false, native_endian_t>;
  template <fixed_string Name> using u3  = int_field<Name, 3,  false, native_endian_t>;
  template <fixed_string Name> using u4  = int_field<Name, 4,  false, native_endian_t>;
  template <fixed_string Name> using u5  = int_field<Name, 5,  false, native_endian_t>;
  template <fixed_string Name> using u6  = int_field<Name, 6,  false, native_endian_t>;
  template <fixed_string Name> using u7  = int_field<Name, 7,  false, native_endian_t>;
  template <fixed_string Name> using u8  = int_field<Name, 8,  false, native_endian_t>;
  template <fixed_string Name> using u16 = int_field<Name, 16, false, native_endian_t>;
  template <fixed_string Name> using u32 = int_field<Name, 32, false, native_endian_t>;
  template <fixed_string Name> using u64 = int_field<Name, 64, false, native_endian_t>;

  template <fixed_string Name> using i8  = int_field<Name, 8,  true, native_endian_t>;
  template <fixed_string Name> using i16 = int_field<Name, 16, true, native_endian_t>;
  template <fixed_string Name> using i32 = int_field<Name, 32, true, native_endian_t>;
  template <fixed_string Name> using i64 = int_field<Name, 64, true, native_endian_t>;

  template <fixed_string Name> using le_u16 = int_field<Name, 16, false, little_endian_t>;
  template <fixed_string Name> using le_u32 = int_field<Name, 32, false, little_endian_t>;
  template <fixed_string Name> using le_u64 = int_field<Name, 64, false, little_endian_t>;
  template <fixed_string Name> using be_u16 = int_field<Name, 16, false, big_endian_t>;
  template <fixed_string Name> using be_u32 = int_field<Name, 32, false, big_endian_t>;
  template <fixed_string Name> using be_u64 = int_field<Name, 64, false, big_endian_t>;

  template <std::size_t Bits, fixed_string Name> using ubits = int_field<Name, Bits, false, native_endian_t>;
  template <std::size_t Bits, fixed_string Name> using ibits = int_field<Name, Bits, true,  native_endian_t>;

  template <fixed_string Name, std::size_t N> using bytes = bytes_field<Name, N>;
  template <typename Packet, fixed_string Name> using subpacket = subpacket_field<Packet, Name>;

   
  // packet<...> definition
   
  template <typename... Fields>
  struct packet {
    static_assert(detail::all_unique_names<Fields...>::value, "duplicate field name in packet");
    static constexpr std::size_t field_count = sizeof...(Fields);

    static constexpr std::size_t total_bits = (0ull + ... + Fields::bits);
    static constexpr std::size_t total_bytes = (total_bits + 7u) >> 3;

    using fields = detail::type_list<Fields...>;

    static constexpr auto offsets_bits = []() consteval {
      std::array<std::size_t, field_count> offs{};
      std::size_t acc = 0;
      std::size_t i = 0;
      ((offs[i++] = acc, acc += Fields::bits), ...);
      return offs;
    }();

    static constexpr auto sizes_bits = []() consteval {
      std::array<std::size_t, field_count> sz{};
      std::size_t i = 0;
      ((sz[i++] = Fields::bits), ...);
      return sz;
    }();

    // bounds validation: every field fits within total_bytes (by construction), but also
    // bit-window reads never touch beyond end for int_fields.
    static consteval bool validate() {
      // Validate integer fields bit windows fit for their reads (need_bytes <= remaining bytes).
      constexpr std::size_t n = field_count;
      for (std::size_t i = 0; i < n; ++i) {
        // We can't directly inspect type at runtime in consteval loop, so rely on byte-window
        // checks in get/set which are compile-time instantiations.
      }
      return true;
    }
    static_assert(validate());

    template <std::size_t I>
    using field_t = std::tuple_element_t<I, std::tuple<Fields...>>;

    template <fixed_string Name>
    static constexpr std::size_t index_of = detail::find_field_index_v<Name, Fields...>;

    template <fixed_string Name>
    static constexpr bool has = (index_of<Name> != static_cast<std::size_t>(-1));
  };

   
  // spans (std::span if avail)
   
#if __has_include(<span>)
  template <typename T, std::size_t Extent = std::dynamic_extent>
  using span = std::span<T, Extent>;
#else
  inline constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

  template <typename T, std::size_t Extent = dynamic_extent>
  class span {
    T* p_{};
    std::size_t n_{};
  public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;

    constexpr span() noexcept = default;
    constexpr span(T* p, std::size_t n) noexcept : p_(p), n_(n) {}
    template <std::size_t N, typename = std::enable_if_t<Extent == dynamic_extent || Extent == N>>
    constexpr span(T (&arr)[N]) noexcept : p_(arr), n_(N) {}

    constexpr T* data() const noexcept { return p_; }
    constexpr std::size_t size() const noexcept { return n_; }
    constexpr T& operator[](std::size_t i) const noexcept { return p_[i]; }
  };
#endif

   
  // bytes_ref (mutable/const)
   
  template <std::size_t N, bool Mutable>
  struct bytes_ref {
    using pointer = std::conditional_t<Mutable, std::byte*, std::byte const*>;
    using elem    = std::conditional_t<Mutable, std::byte, std::byte const>;
    pointer p{};

    MAD_FORCEINLINE constexpr pointer data() const noexcept { return p; }
    MAD_FORCEINLINE constexpr std::size_t size() const noexcept { return N; }
    MAD_FORCEINLINE constexpr span<elem, N> as_span() const noexcept { return span<elem, N>(p, N); }
  };

   
  // packet view
   
  namespace detail {
    template <typename Packet, bool Mutable>
    class view_base;

    template <typename Packet, bool Mutable>
    struct ptr_type;
    template <typename Packet>
    struct ptr_type<Packet, true>  { using type = std::byte*; };
    template <typename Packet>
    struct ptr_type<Packet, false> { using type = std::byte const*; };

    template <typename Packet, bool Mutable>
    using ptr_type_t = typename ptr_type<Packet, Mutable>::type;

    template <typename Packet, std::size_t I>
    inline constexpr std::size_t field_bit_offset_v = Packet::offsets_bits[I];

    template <typename Packet, std::size_t I>
    inline constexpr std::size_t field_bit_size_v = Packet::sizes_bits[I];

    template <typename Packet, std::size_t I>
    inline constexpr std::size_t field_byte_offset_v = field_bit_offset_v<Packet, I> >> 3;

    template <typename Packet, std::size_t I>
    inline constexpr std::size_t field_bit_shift_v = field_bit_offset_v<Packet, I> & 7u;

    template <typename Packet, std::size_t I>
    struct field_access;

    template <typename Packet, bool Mutable, std::size_t I>
    MAD_FORCEINLINE constexpr auto get_impl(ptr_type_t<Packet, Mutable> base) noexcept {
      using F = typename Packet::template field_t<I>;
      constexpr std::size_t bit_off = field_bit_offset_v<Packet, I>;
      if constexpr (F::kind == field_kind::int_bits) {
        constexpr std::size_t bits = F::bits;
        constexpr std::size_t shift = bit_off & 7u;
        constexpr std::size_t byte = bit_off >> 3;

        if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
          using U = detail::uint_for_bits_t<bits>;
          U x = load_pod_le<U>(reinterpret_cast<std::byte const*>(base) + byte);
          if constexpr (!detail::is_native_endian<typename F::endian>) {
            if constexpr (detail::need_bswap<typename F::endian>) x = bswap(x);
          }
          if constexpr (F::is_signed) {
            using S = detail::int_for_bits_t<bits>;
            return static_cast<std::int64_t>(static_cast<S>(x));
          } else {
            return static_cast<std::uint64_t>(x);
          }
        } else {
          static_assert(detail::is_native_endian<typename F::endian>, "non-byte-multiple fields cannot specify endianness");
          std::uint64_t x = read_bits_le<bit_off, bits>(reinterpret_cast<std::byte const*>(base));
          return detail::sign_extend<bits, F::is_signed>(x);
        }
      } else if constexpr (F::kind == field_kind::bytes) {
        constexpr std::size_t byte = bit_off >> 3;
        return bytes_ref<F::bytes, Mutable>{ base + byte };
      } else if constexpr (F::kind == field_kind::subpacket) {
        constexpr std::size_t byte = bit_off >> 3;
        using Sub = typename F::packet;
        return view_base<Sub, Mutable>(base + byte);
      } else {
        // pad
        return;
      }
    }

    template <typename Packet, bool Mutable, std::size_t I, typename V>
    MAD_FORCEINLINE constexpr void set_impl(ptr_type_t<Packet, Mutable> base, V&& v) noexcept {
      static_assert(Mutable, "attempting to set on const view");
      using F = typename Packet::template field_t<I>;
      constexpr std::size_t bit_off = field_bit_offset_v<Packet, I>;
      if constexpr (F::kind == field_kind::int_bits) {
        constexpr std::size_t bits = F::bits;
        constexpr std::size_t shift = bit_off & 7u;
        constexpr std::size_t byte = bit_off >> 3;

        if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
          using U = detail::uint_for_bits_t<bits>;
          U x = static_cast<U>(detail::to_u64(std::forward<V>(v)));
          if constexpr (!detail::is_native_endian<typename F::endian>) {
            if constexpr (detail::need_bswap<typename F::endian>) x = bswap(x);
          }
          store_pod_le<U>(base + byte, x);
        } else {
          static_assert(detail::is_native_endian<typename F::endian>, "non-byte-multiple fields cannot specify endianness");
          write_bits_le<bit_off, bits>(base, detail::to_u64(std::forward<V>(v)));
        }
      } else if constexpr (F::kind == field_kind::bytes) {
        // bytes field doesn't accept "set" (use returned bytes_view to write)
        static_assert(sizeof(F) == 0, "bytes field: use get<name>().as_span() / .data() to write");
      } else if constexpr (F::kind == field_kind::subpacket) {
        static_assert(sizeof(F) == 0, "subpacket field: assign via nested view");
      } else {
        // pad: ignore
      }
    }

    template <typename Packet, bool Mutable>
    class view_base {
      ptr_type_t<Packet, Mutable> base_{};
    public:
      using packet_type = Packet;
      using pointer = ptr_type_t<Packet, Mutable>;

      MAD_FORCEINLINE constexpr view_base() noexcept = default;
      MAD_FORCEINLINE constexpr explicit view_base(pointer p) noexcept : base_(p) {}

      MAD_FORCEINLINE constexpr pointer data() const noexcept { return base_; }
      static constexpr std::size_t size_bytes() noexcept { return Packet::total_bytes; }
      static constexpr std::size_t size_bits() noexcept { return Packet::total_bits; }

      template <fixed_string Name>
      MAD_FORCEINLINE constexpr auto get() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot get a pad field");
        return get_impl<Packet, Mutable, idx>(base_);
      }

      template <fixed_string Name, typename V>
      MAD_FORCEINLINE constexpr void set(V&& v) const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot set a pad field");
        set_impl<Packet, Mutable, idx>(base_, std::forward<V>(v));
      }

      // fast indexed access (for TMP-heavy codegen)
      template <std::size_t I>
      MAD_FORCEINLINE constexpr auto get_i() const noexcept {
        static_assert(I < Packet::field_count);
        using F = typename Packet::template field_t<I>;
        static_assert(F::kind != field_kind::pad, "cannot get a pad field");
        return get_impl<Packet, Mutable, I>(base_);
      }

      template <std::size_t I, typename V>
      MAD_FORCEINLINE constexpr void set_i(V&& v) const noexcept {
        static_assert(I < Packet::field_count);
        using F = typename Packet::template field_t<I>;
        static_assert(F::kind != field_kind::pad, "cannot set a pad field");
        set_impl<Packet, Mutable, I>(base_, std::forward<V>(v));
      }
    };

  } // namespace detail

  template <typename Packet>
  using view = detail::view_base<Packet, true>;

  template <typename Packet>
  using cview = detail::view_base<Packet, false>;

   
  // Safe construction helpers
   
  template <typename Packet>
  MAD_FORCEINLINE constexpr cview<Packet> make_view(std::byte const* data, std::size_t n) noexcept {
    MAD_ASSERT(n >= Packet::total_bytes);
    return cview<Packet>(data);
  }

  template <typename Packet>
  MAD_FORCEINLINE constexpr view<Packet> make_view(std::byte* data, std::size_t n) noexcept {
    MAD_ASSERT(n >= Packet::total_bytes);
    return view<Packet>(data);
  }

   
  // Member mapping codec (optional)
   
  // Map packet fields to user struct members (no reflection needed).
  // Example:
  //   struct Msg { uint8_t a; uint16_t b; uint32_t c; };
  //   using P = mad::packet< mad::u8<"a">, mad::be_u16<"b">, mad::u32<"c"> >;
  //   using C = mad::codec<P, Msg,
  //            mad::map<"a", &Msg::a>,
  //            mad::map<"b", &Msg::b>,
  //            mad::map<"c", &Msg::c> >;
  //   std::byte buf[P::total_bytes]{};
  //   C::encode(Msg{1,2,3}, buf);
  //   Msg out{};
  //   C::decode(buf, out);

  template <fixed_string Name, auto MemberPtr>
  struct map {
    static constexpr auto name = Name;
    static constexpr auto ptr = MemberPtr;
  };

  namespace detail {
    template <typename T>
    struct member_pointer_traits;

    template <typename C, typename M>
    struct member_pointer_traits<M C::*> {
      using class_type = C;
      using member_type = M;
    };

    template <typename Packet, typename Obj, typename... Maps>
    struct codec_impl {
      static_assert(std::is_standard_layout_v<Obj> || std::is_trivial_v<Obj>, "codec: Obj should be a simple POD-ish type");
      static constexpr std::size_t size_bytes = Packet::total_bytes;

      MAD_FORCEINLINE static void encode(Obj const& o, std::byte* out, std::size_t n) noexcept {
        MAD_ASSERT(n >= size_bytes);
        view<Packet> v(out);
        (encode_one<Maps>(v, o), ...);
      }

      MAD_FORCEINLINE static void decode(std::byte const* in, std::size_t n, Obj& o) noexcept {
        MAD_ASSERT(n >= size_bytes);
        cview<Packet> v(in);
        (decode_one<Maps>(v, o), ...);
      }

    private:
      template <typename Map>
      MAD_FORCEINLINE static void encode_one(view<Packet> const& v, Obj const& o) noexcept {
        using MP = std::remove_cv_t<decltype(Map::ptr)>;
        using traits = member_pointer_traits<MP>;
        static_assert(std::is_same_v<typename traits::class_type, Obj>, "codec map member pointer must belong to Obj");
        auto const& mem = o.*(Map::ptr);
        if constexpr (Packet::template has<Map::name>) {
          // if bytes field, require mem to be contiguous byte array-like
          using F = typename Packet::template field_t<Packet::template index_of<Map::name>>;
          if constexpr (F::kind == field_kind::bytes) {
            auto bv = v.template get<Map::name>();
            // Accept std::array<std::byte,N>, std::array<uint8_t,N>, uint8_t[N], std::byte[N]
            if constexpr (std::is_array_v<detail::remove_cvref_t<decltype(mem)>>) {
              constexpr std::size_t N = std::extent_v<detail::remove_cvref_t<decltype(mem)>>;
              static_assert(N == F::bytes, "codec: bytes member array size mismatch");
              std::memcpy(bv.data(), &mem[0], N);
            } else {
              // std::array
              static_assert(sizeof(mem) == F::bytes, "codec: bytes member size mismatch");
              std::memcpy(bv.data(), &mem, F::bytes);
            }
          } else if constexpr (F::kind == field_kind::subpacket) {
            // not supported via map; user can encode nested separately
            static_assert(sizeof(F) == 0, "codec: subpacket mapping not supported; encode nested via view");
          } else {
            v.template set<Map::name>(mem);
          }
        } else {
          static_assert(Packet::template has<Map::name>, "codec map: field name not in Packet");
        }
      }

      template <typename Map>
      MAD_FORCEINLINE static void decode_one(cview<Packet> const& v, Obj& o) noexcept {
        using MP = std::remove_cv_t<decltype(Map::ptr)>;
        using traits = member_pointer_traits<MP>;
        static_assert(std::is_same_v<typename traits::class_type, Obj>, "codec map member pointer must belong to Obj");
        auto& mem = o.*(Map::ptr);
        using F = typename Packet::template field_t<Packet::template index_of<Map::name>>;
        if constexpr (F::kind == field_kind::bytes) {
          auto bv = v.template get<Map::name>();
          if constexpr (std::is_array_v<detail::remove_cvref_t<decltype(mem)>>) {
            constexpr std::size_t N = std::extent_v<detail::remove_cvref_t<decltype(mem)>>;
            static_assert(N == F::bytes, "codec: bytes member array size mismatch");
            std::memcpy(&mem[0], bv.data(), N);
          } else {
            static_assert(sizeof(mem) == F::bytes, "codec: bytes member size mismatch");
            std::memcpy(&mem, bv.data(), F::bytes);
          }
        } else if constexpr (F::kind == field_kind::subpacket) {
          static_assert(sizeof(F) == 0, "codec: subpacket mapping not supported; decode nested via view");
        } else {
          auto val = v.template get<Map::name>();
          if constexpr (std::is_integral_v<detail::remove_cvref_t<decltype(mem)>> || std::is_enum_v<detail::remove_cvref_t<decltype(mem)>>) {
            mem = static_cast<detail::remove_cvref_t<decltype(mem)>>(val);
          } else {
            mem = val;
          }
        }
      }
    };

  } // namespace detail

  template <typename Packet, typename Obj, typename... Maps>
  struct codec : detail::codec_impl<Packet, Obj, Maps...> {};

   // Embedded MMIO register views
   //
  // These provide the same named-field get/set interface, but operate on
  // volatile memory-mapped IO regions (typical embedded registers).
  //
  // Controls:
  //   - Define MADPACKET_STRICT_MMIO to force byte-wise accesses (avoids type-punned volatile loads).
  //   - Define MAD_MMIO_BARRIER() for optional fences (defaults to no-op).
  //
#ifndef MAD_MMIO_BARRIER
  #define MAD_MMIO_BARRIER() ((void)0)
#endif

  namespace detail {

    MAD_FORCEINLINE unsigned char vload_u8(volatile std::byte const* p) noexcept {
      // read volatile byte once (avoid volatile std::byte lvalue-to-rvalue issues)
      return *reinterpret_cast<volatile unsigned char const*>(p);
    }
    MAD_FORCEINLINE void vstore_u8(volatile std::byte* p, unsigned char v) noexcept {
      *reinterpret_cast<volatile unsigned char*>(p) = v;
    }

    template <std::size_t N, bool Mutable>
    struct mmio_bytes_ref {
      using pointer = std::conditional_t<Mutable, volatile std::byte*, volatile std::byte const*>;
      pointer p{};
      MAD_FORCEINLINE constexpr pointer data() const noexcept { return p; }
      MAD_FORCEINLINE constexpr std::size_t size() const noexcept { return N; }
    };

    template <typename U, std::size_t BaseAlign>
    MAD_FORCEINLINE U mmio_load_pod(volatile std::byte const* p) noexcept {
      static_assert(std::is_integral_v<U>, "mmio_load_pod expects integral U");
#if !defined(MADPACKET_STRICT_MMIO)
      if constexpr (BaseAlign >= alignof(U)) {
        if ((reinterpret_cast<std::uintptr_t>(p) & (alignof(U) - 1u)) == 0u) {
          // typed volatile load (fast path)
          return *reinterpret_cast<volatile U const*>(p);
        }
      }
#endif
      // bytewise volatile load: mimic memcpy semantics on this host endianness
      U x{};
      if constexpr (std::endian::native == std::endian::little) {
        MAD_UNROLL
        for (std::size_t i = 0; i < sizeof(U); ++i) {
          x = static_cast<U>(x | (static_cast<U>(vload_u8(p + i)) << (i * 8)));
        }
      } else {
        MAD_UNROLL
        for (std::size_t i = 0; i < sizeof(U); ++i) {
          x = static_cast<U>((x << 8) | static_cast<U>(vload_u8(p + i)));
        }
      }
      return x;
    }

    template <typename U, std::size_t BaseAlign>
    MAD_FORCEINLINE void mmio_store_pod(volatile std::byte* p, U x) noexcept {
      static_assert(std::is_integral_v<U>, "mmio_store_pod expects integral U");
#if !defined(MADPACKET_STRICT_MMIO)
      if constexpr (BaseAlign >= alignof(U)) {
        if ((reinterpret_cast<std::uintptr_t>(p) & (alignof(U) - 1u)) == 0u) {
          *reinterpret_cast<volatile U*>(p) = x;
          return;
        }
      }
#endif
      // bytewise volatile store: mimic memcpy semantics
      if constexpr (std::endian::native == std::endian::little) {
        MAD_UNROLL
        for (std::size_t i = 0; i < sizeof(U); ++i) {
          vstore_u8(p + i, static_cast<unsigned char>((static_cast<std::make_unsigned_t<U>>(x) >> (i * 8)) & 0xFFu));
        }
      } else {
        MAD_UNROLL
        for (std::size_t i = 0; i < sizeof(U); ++i) {
          const std::size_t sh = (sizeof(U) - 1u - i) * 8u;
          vstore_u8(p + i, static_cast<unsigned char>((static_cast<std::make_unsigned_t<U>>(x) >> sh) & 0xFFu));
        }
      }
    }

    template <std::size_t N>
    MAD_FORCEINLINE std::uint64_t mmio_load_u64_le_n(volatile std::byte const* p) noexcept {
      // always assemble LE byte-stream numeric (bitfield semantics)
      std::uint64_t x = 0;
      MAD_UNROLL
      for (std::size_t i = 0; i < N; ++i) {
        x |= (static_cast<std::uint64_t>(vload_u8(p + i)) << (i * 8));
      }
      return x;
    }

    template <std::size_t N>
    MAD_FORCEINLINE void mmio_store_u64_le_n(volatile std::byte* p, std::uint64_t x) noexcept {
      MAD_UNROLL
      for (std::size_t i = 0; i < N; ++i) {
        vstore_u8(p + i, static_cast<unsigned char>((x >> (i * 8)) & 0xFFu));
      }
    }

    template <std::size_t BitOffset, std::size_t BitCount>
    MAD_FORCEINLINE std::uint64_t mmio_read_bits_le(volatile std::byte const* base) noexcept {
      using W = bit_window<BitOffset, BitCount>;
      std::array<std::byte, W::need_bytes> tmp{};
      MAD_UNROLL
      for (std::size_t i = 0; i < W::need_bytes; ++i) {
        tmp[i] = static_cast<std::byte>(vload_u8(base + W::byte + i));
      }
      return read_bits_le<W::shift, BitCount>(tmp.data());
    }

    template <std::size_t BitOffset, std::size_t BitCount>
    MAD_FORCEINLINE void mmio_write_bits_le(volatile std::byte* base, std::uint64_t value) noexcept {
      using W = bit_window<BitOffset, BitCount>;
      std::array<std::byte, W::need_bytes> tmp{};
      MAD_UNROLL
      for (std::size_t i = 0; i < W::need_bytes; ++i) {
        tmp[i] = static_cast<std::byte>(vload_u8(base + W::byte + i));
      }
      write_bits_le<W::shift, BitCount>(tmp.data(), value);
      MAD_UNROLL
      for (std::size_t i = 0; i < W::need_bytes; ++i) {
        vstore_u8(base + W::byte + i, std::to_integer<unsigned char>(tmp[i]));
      }
    }

    template <typename Packet, bool Mutable>
    struct vptr_type;
    template <typename Packet>
    struct vptr_type<Packet, true>  { using type = volatile std::byte*; };
    template <typename Packet>
    struct vptr_type<Packet, false> { using type = volatile std::byte const*; };
    template <typename Packet, bool Mutable>
    using vptr_type_t = typename vptr_type<Packet, Mutable>::type;

    template <typename Packet, bool Mutable, std::size_t I, typename Bus, std::size_t BaseAlign>
    MAD_FORCEINLINE auto vget_impl(vptr_type_t<Packet, Mutable> base) noexcept {
      using F = typename Packet::template field_t<I>;
      constexpr std::size_t bit_off = field_bit_offset_v<Packet, I>;
      if constexpr (F::kind == field_kind::int_bits) {
        constexpr std::size_t bits = F::bits;
        constexpr std::size_t shift = bit_off & 7u;
        constexpr std::size_t byte = bit_off >> 3;

        if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
          using U = detail::uint_for_bits_t<bits>;
          U x = mmio_load_pod<U, BaseAlign>(base + byte);
          if constexpr (!detail::is_native_endian<typename F::endian>) {
            if constexpr (detail::need_bswap<typename F::endian>) x = bswap(x);
          }
          if constexpr (F::is_signed) {
            using S = detail::int_for_bits_t<bits>;
            return static_cast<std::int64_t>(static_cast<S>(x));
          } else {
            return static_cast<std::uint64_t>(x);
          }
        } else {
          static_assert(detail::is_native_endian<typename F::endian>, "non-byte-multiple fields cannot specify endianness");

          constexpr std::size_t bus_bits = Bus::bits;
          constexpr std::size_t bus_bytes = Bus::bytes;
          constexpr std::size_t word_idx = bit_off / bus_bits;
          constexpr std::size_t bit_in_word = bit_off - (word_idx * bus_bits);
          constexpr bool fits_one = (bit_in_word + bits) <= bus_bits;

          if constexpr (fits_one && (bus_bits <= 64) && (bus_bits % 8 == 0)) {
            const volatile std::byte* wp = base + (word_idx * bus_bytes);
            std::uint64_t w = mmio_load_u64_le_n<bus_bytes>(wp);
            const std::uint64_t m = (mask64<bits> << bit_in_word);
            const std::uint64_t x = (w & m) >> bit_in_word;
            return detail::sign_extend<bits, F::is_signed>(x);
          } else {
            std::uint64_t x = mmio_read_bits_le<bit_off, bits>(base);
            return detail::sign_extend<bits, F::is_signed>(x);
          }
        }
      } else if constexpr (F::kind == field_kind::bytes) {
        constexpr std::size_t byte = bit_off >> 3;
        return mmio_bytes_ref<F::bytes, Mutable>{ base + byte };
      } else {
        // subpacket + pad handled in reg_view_base
        return;
      }
    }

    template <typename Packet, bool Mutable, std::size_t I, typename V, typename Bus, std::size_t BaseAlign>
    MAD_FORCEINLINE void vset_impl(vptr_type_t<Packet, Mutable> base, V&& v) noexcept {
      static_assert(Mutable, "attempting to set on const reg view");
      using F = typename Packet::template field_t<I>;
      constexpr std::size_t bit_off = field_bit_offset_v<Packet, I>;
      if constexpr (F::kind == field_kind::int_bits) {
        constexpr std::size_t bits = F::bits;
        constexpr std::size_t shift = bit_off & 7u;
        constexpr std::size_t byte = bit_off >> 3;

        if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
          using U = detail::uint_for_bits_t<bits>;
          U x = static_cast<U>(detail::to_u64(std::forward<V>(v)));
          if constexpr (!detail::is_native_endian<typename F::endian>) {
            if constexpr (detail::need_bswap<typename F::endian>) x = bswap(x);
          }
          MAD_MMIO_BARRIER();
          mmio_store_pod<U, BaseAlign>(const_cast<volatile std::byte*>(base + byte), x);
          MAD_MMIO_BARRIER();
        } else {
          static_assert(detail::is_native_endian<typename F::endian>, "non-byte-multiple fields cannot specify endianness");

          constexpr std::size_t bus_bits = Bus::bits;
          constexpr std::size_t bus_bytes = Bus::bytes;
          constexpr std::size_t word_idx = bit_off / bus_bits;
          constexpr std::size_t bit_in_word = bit_off - (word_idx * bus_bits);
          constexpr bool fits_one = (bit_in_word + bits) <= bus_bits;

          const std::uint64_t value = detail::to_u64(std::forward<V>(v)) & mask64<bits>;

          if constexpr (fits_one && (bus_bits <= 64) && (bus_bits % 8 == 0)) {
            volatile std::byte* wp = const_cast<volatile std::byte*>(base + (word_idx * bus_bytes));
            std::uint64_t w = mmio_load_u64_le_n<bus_bytes>(wp);
            const std::uint64_t m = (mask64<bits> << bit_in_word);
            w = (w & ~m) | (value << bit_in_word);
            MAD_MMIO_BARRIER();
            mmio_store_u64_le_n<bus_bytes>(wp, w);
            MAD_MMIO_BARRIER();
          } else {
            MAD_MMIO_BARRIER();
            mmio_write_bits_le<bit_off, bits>(const_cast<volatile std::byte*>(base), value);
            MAD_MMIO_BARRIER();
          }
        }
      } else if constexpr (F::kind == field_kind::bytes) {
        static_assert(sizeof(F) == 0, "bytes field: write via get<name>().data()/size() and volatile stores");
      } else if constexpr (F::kind == field_kind::subpacket) {
        static_assert(sizeof(F) == 0, "subpacket field: assign via nested reg view");
      } else {
        // pad
      }
    }

  } // namespace detail

  namespace reg {

    template <typename WordT>
    struct bus {
      using word = WordT;
      static constexpr std::size_t bytes = sizeof(word);
      static constexpr std::size_t bits = bytes * 8;
      static constexpr std::size_t align = alignof(word);
      static_assert(bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8, "bus word must be 1/2/4/8 bytes");
    };

    using bus8  = bus<std::uint8_t>;
    using bus16 = bus<std::uint16_t>;
    using bus32 = bus<std::uint32_t>;
    using bus64 = bus<std::uint64_t>;

    template <typename Packet, bool Mutable, typename Bus, std::size_t BaseAlign>
    class reg_view_base {
      detail::vptr_type_t<Packet, Mutable> base_{};
    public:
      using packet_type = Packet;
      using bus_type = Bus;
      using pointer = detail::vptr_type_t<Packet, Mutable>;
      static constexpr std::size_t base_align = BaseAlign;

      MAD_FORCEINLINE constexpr reg_view_base() noexcept = default;
      MAD_FORCEINLINE constexpr explicit reg_view_base(pointer p) noexcept : base_(p) {}

      MAD_FORCEINLINE constexpr pointer data() const noexcept { return base_; }
      static constexpr std::size_t size_bytes() noexcept { return Packet::total_bytes; }
      static constexpr std::size_t size_bits() noexcept { return Packet::total_bits; }

      template <fixed_string Name>
      MAD_FORCEINLINE auto get() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot get a pad field");

        if constexpr (F::kind == field_kind::subpacket) {
          constexpr std::size_t bit_off = detail::field_bit_offset_v<Packet, idx>;
          constexpr std::size_t byte = bit_off >> 3;
          using Sub = typename F::packet;
          return reg_view_base<Sub, Mutable, Bus, BaseAlign>(base_ + byte);
        } else {
          return detail::vget_impl<Packet, Mutable, idx, Bus, BaseAlign>(base_);
        }
      }

      template <fixed_string Name, typename V>
      MAD_FORCEINLINE void set(V&& v) const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot set a pad field");
        detail::vset_impl<Packet, Mutable, idx, V, Bus, BaseAlign>(base_, std::forward<V>(v));
      }

      template <std::size_t I>
      MAD_FORCEINLINE auto get_i() const noexcept {
        static_assert(I < Packet::field_count);
        using F = typename Packet::template field_t<I>;
        static_assert(F::kind != field_kind::pad, "cannot get a pad field");
        if constexpr (F::kind == field_kind::subpacket) {
          constexpr std::size_t bit_off = detail::field_bit_offset_v<Packet, I>;
          constexpr std::size_t byte = bit_off >> 3;
          using Sub = typename F::packet;
          return reg_view_base<Sub, Mutable, Bus, BaseAlign>(base_ + byte);
        } else {
          return detail::vget_impl<Packet, Mutable, I, Bus, BaseAlign>(base_);
        }
      }

      template <std::size_t I, typename V>
      MAD_FORCEINLINE void set_i(V&& v) const noexcept {
        static_assert(I < Packet::field_count);
        using F = typename Packet::template field_t<I>;
        static_assert(F::kind != field_kind::pad, "cannot set a pad field");
        detail::vset_impl<Packet, Mutable, I, V, Bus, BaseAlign>(base_, std::forward<V>(v));
      }
    };

    template <typename Packet, typename Bus = bus32, std::size_t BaseAlign = Bus::align>
    using view = reg_view_base<Packet, true, Bus, BaseAlign>;

    template <typename Packet, typename Bus = bus32, std::size_t BaseAlign = Bus::align>
    using cview = reg_view_base<Packet, false, Bus, BaseAlign>;

    template <typename Packet, typename Bus = bus32, std::size_t BaseAlign = Bus::align>
    MAD_FORCEINLINE constexpr cview<Packet, Bus, BaseAlign> make_view(volatile void const* addr) noexcept {
      auto p = reinterpret_cast<volatile std::byte const*>(addr);
      MAD_ASSERT((reinterpret_cast<std::uintptr_t>(p) & (BaseAlign - 1u)) == 0u);
      return cview<Packet, Bus, BaseAlign>(p);
    }

    template <typename Packet, typename Bus = bus32, std::size_t BaseAlign = Bus::align>
    MAD_FORCEINLINE constexpr view<Packet, Bus, BaseAlign> make_view(volatile void* addr) noexcept {
      auto p = reinterpret_cast<volatile std::byte*>(addr);
      MAD_ASSERT((reinterpret_cast<std::uintptr_t>(p) & (BaseAlign - 1u)) == 0u);
      return view<Packet, Bus, BaseAlign>(p);
    }

  } // namespace reg

   // File-format helpers (bounded views, offsets, tables, strings)
   namespace file {

    template <typename Packet, bool Mutable>
    class view_base {
      detail::ptr_type_t<Packet, Mutable> base_{};
      std::byte const* origin_{};
      std::size_t size_{};
    public:
      using packet_type = Packet;
      using pointer = detail::ptr_type_t<Packet, Mutable>;

      MAD_FORCEINLINE constexpr view_base() noexcept = default;

      MAD_FORCEINLINE constexpr view_base(pointer base, std::byte const* origin, std::size_t size) noexcept
        : base_(base), origin_(origin), size_(size) {}

      MAD_FORCEINLINE constexpr pointer data() const noexcept { return base_; }
      MAD_FORCEINLINE constexpr std::byte const* origin() const noexcept { return origin_; }
      MAD_FORCEINLINE constexpr std::size_t file_size() const noexcept { return size_; }

      static constexpr std::size_t size_bytes() noexcept { return Packet::total_bytes; }
      static constexpr std::size_t size_bits() noexcept { return Packet::total_bits; }

      MAD_FORCEINLINE constexpr bool in_bounds(std::size_t off, std::size_t len) const noexcept {
        return off <= size_ && len <= (size_ - off);
      }

      template <fixed_string Name>
      MAD_FORCEINLINE constexpr auto get() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot get a pad field");
        return detail::get_impl<Packet, Mutable, idx>(base_);
      }

      template <fixed_string Name, typename V>
      MAD_FORCEINLINE constexpr void set(V&& v) const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot set a pad field");
        detail::set_impl<Packet, Mutable, idx>(base_, std::forward<V>(v));
      }

      template <typename OtherPacket>
      MAD_FORCEINLINE constexpr auto at(std::size_t offset) const noexcept {
        MAD_ASSERT(in_bounds(offset, OtherPacket::total_bytes));
        return view_base<OtherPacket, Mutable>(
          const_cast<detail::ptr_type_t<OtherPacket, Mutable>>(reinterpret_cast<std::byte const*>(origin_) + offset),
          origin_, size_);
      }

      template <fixed_string OffsetFieldName, typename TargetPacket, std::ptrdiff_t Add = 0>
      MAD_FORCEINLINE constexpr auto follow() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<OffsetFieldName>;
        static_assert(idx != static_cast<std::size_t>(-1), "offset field not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind == field_kind::int_bits, "follow(): offset field must be an integer field");
        auto off64 = detail::to_u64(detail::get_impl<Packet, false, idx>(base_));
        std::size_t off = static_cast<std::size_t>(off64 + static_cast<std::uint64_t>(Add));
        MAD_ASSERT(in_bounds(off, TargetPacket::total_bytes));
        return view_base<TargetPacket, Mutable>(
          const_cast<detail::ptr_type_t<TargetPacket, Mutable>>(reinterpret_cast<std::byte const*>(origin_) + off),
          origin_, size_);
      }

      template <fixed_string OffsetFieldName, typename TargetPacket, std::ptrdiff_t Add = 0>
      MAD_FORCEINLINE constexpr auto follow_rel() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<OffsetFieldName>;
        static_assert(idx != static_cast<std::size_t>(-1), "offset field not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind == field_kind::int_bits, "follow_rel(): offset field must be an integer field");
        auto off64 = detail::to_u64(detail::get_impl<Packet, false, idx>(base_));
        auto base_off = static_cast<std::size_t>(reinterpret_cast<std::byte const*>(base_) - origin_);
        std::size_t off = static_cast<std::size_t>(base_off + off64 + static_cast<std::uint64_t>(Add));
        MAD_ASSERT(in_bounds(off, TargetPacket::total_bytes));
        return view_base<TargetPacket, Mutable>(
          const_cast<detail::ptr_type_t<TargetPacket, Mutable>>(reinterpret_cast<std::byte const*>(origin_) + off),
          origin_, size_);
      }

#if __has_include(<string_view>)
      template <fixed_string BytesFieldName>
      MAD_FORCEINLINE std::string_view strz() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<BytesFieldName>;
        static_assert(idx != static_cast<std::size_t>(-1), "bytes field not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind == field_kind::bytes, "strz(): field must be bytes<N>");
        auto b = detail::get_impl<Packet, false, idx>(base_);
        auto p = reinterpret_cast<char const*>(b.data());
        const void* z = std::memchr(p, 0, F::bytes);
        std::size_t n = z ? static_cast<std::size_t>(reinterpret_cast<char const*>(z) - p) : F::bytes;
        return std::string_view(p, n);
      }
#endif

      template <fixed_string BytesFieldName, fixed_string Magic>
      MAD_FORCEINLINE constexpr bool magic_eq() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<BytesFieldName>;
        static_assert(idx != static_cast<std::size_t>(-1), "bytes field not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind == field_kind::bytes, "magic_eq(): field must be bytes<N>");
        static_assert(Magic.len() <= F::bytes, "magic too long for field");
        auto b = detail::get_impl<Packet, false, idx>(base_);
        return std::memcmp(b.data(), Magic.c_str(), Magic.len()) == 0;
      }
    };

    template <typename Packet>
    using view = view_base<Packet, true>;
    template <typename Packet>
    using cview = view_base<Packet, false>;

    template <typename Packet>
    MAD_FORCEINLINE constexpr cview<Packet> make_view(std::byte const* data, std::size_t n, std::size_t offset = 0) noexcept {
      MAD_ASSERT(offset <= n);
      MAD_ASSERT((n - offset) >= Packet::total_bytes);
      return cview<Packet>(data + offset, data, n);
    }

    template <typename Packet>
    MAD_FORCEINLINE constexpr view<Packet> make_view(std::byte* data, std::size_t n, std::size_t offset = 0) noexcept {
      MAD_ASSERT(offset <= n);
      MAD_ASSERT((n - offset) >= Packet::total_bytes);
      return view<Packet>(data + offset, data, n);
    }

    template <typename EntryPacket, bool Mutable>
    class table_view_base {
      detail::ptr_type_t<EntryPacket, Mutable> base_{};
      std::byte const* origin_{};
      std::size_t size_{};
      std::size_t count_{};
      std::size_t stride_{};
    public:
      MAD_FORCEINLINE constexpr table_view_base() noexcept = default;

      MAD_FORCEINLINE constexpr table_view_base(detail::ptr_type_t<EntryPacket, Mutable> base,
                                                std::byte const* origin,
                                                std::size_t size,
                                                std::size_t count,
                                                std::size_t stride) noexcept
        : base_(base), origin_(origin), size_(size), count_(count), stride_(stride) {}

      MAD_FORCEINLINE constexpr std::size_t size() const noexcept { return count_; }
      MAD_FORCEINLINE constexpr std::size_t stride() const noexcept { return stride_; }

      MAD_FORCEINLINE constexpr auto operator[](std::size_t i) const noexcept {
        MAD_ASSERT(i < count_);
        std::size_t base_off = static_cast<std::size_t>(reinterpret_cast<std::byte const*>(base_) - origin_);
        std::size_t off = base_off + i * stride_;
        MAD_ASSERT(off <= size_ && (size_ - off) >= EntryPacket::total_bytes);
        return view_base<EntryPacket, Mutable>(
          const_cast<detail::ptr_type_t<EntryPacket, Mutable>>(reinterpret_cast<std::byte const*>(origin_) + off),
          origin_, size_);
      }
    };

    template <typename EntryPacket>
    using table_view = table_view_base<EntryPacket, true>;
    template <typename EntryPacket>
    using ctable_view = table_view_base<EntryPacket, false>;

    template <typename HostPacket, bool Mutable, fixed_string OffsetFieldName, typename EntryPacket>
    MAD_FORCEINLINE constexpr auto follow_table(view_base<HostPacket, Mutable> const& host,
                                                std::size_t count,
                                                std::size_t stride = EntryPacket::total_bytes) noexcept {
      auto off64 = detail::to_u64(host.template get<OffsetFieldName>());
      std::size_t off = static_cast<std::size_t>(off64);
      MAD_ASSERT(host.in_bounds(off, count * stride));
      return table_view_base<EntryPacket, Mutable>(
        const_cast<detail::ptr_type_t<EntryPacket, Mutable>>(reinterpret_cast<std::byte const*>(host.origin()) + off),
        host.origin(), host.file_size(), count, stride);
    }

  } // namespace file

   // MMIO policies: bus-width enforcement + alignment expectations
   //
  // This module provides a volatile view over a packet layout,
  // with opportunistic typed volatile loads/stores for byte-aligned 8/16/32/64
  // fields (unless MADPACKET_STRICT_MMIO is defined).
  //
  // Many embedded targets impose *additional* constraints that are orthogonal
  // to endianness / bit numbering:
  //   - Access *width* constraints: e.g. "all transactions must be 32-bit".
  //     Narrow reads/writes (8/16-bit) can fault, be ignored, or have undefined
  //     effects on some interconnects / bridges.
  //   - Strict alignment: e.g. "32-bit access must be 4-byte aligned", and
  //     unaligned access may fault. Some memory maps also require that the
  //     *base address* is aligned to the bus width for correct decode.
  //
  // This add-on keeps the existing reg_view_base intact and adds a separate,
  // *policy-configurable* register view:
  //
  //   - mad::reg::xview / mad::reg::xcview
  //   - mad::reg::make_xview / mad::reg::make_xcview
  //
  // Features:
  //   1) Read/write width capability masks (allowed transaction sizes).
  //   2) Width selection policies:
  //        - native     : behave like the original reg view (field-sized access).
  //        - enforce_bus: always transact using Bus::word size (RMW for subfields).
  //        - prefer_bus : use bus-word when it reduces transactions, else native.
  //        - minimal_ok : smallest legal width >= field bytes (RMW if larger).
  //   3) Alignment policies:
  //        - unchecked  : no checks; UB if the target faults.
  //        - assert_    : MAD_ASSERT on misalignment.
  //        - trap       : compiler trap on misalignment.
  //        - assume     : compiler "assume" for extra optimization (dangerous).
  //   4) Compile-time layout validation helpers.
  //
  // Notes:
  //   - "Bus" in this module means *transaction granularity*; it does not claim
  //     anything about the physical interconnect.
  //   - If MADPACKET_STRICT_MMIO is defined, typed volatile loads/stores are
  //     disabled in the original module. This add-on still *tries* to honor the
  //     requested transaction width, but if typed volatile loads are disabled
  //     and no platform intrinsic is available, it will fall back to byte-wise
  //     volatile accesses. To hard-fail instead, define MADPACKET_MMIO_HARDWIDTH.
  //
  namespace reg {

       // Alignment + width policies
       enum class align_policy : std::uint8_t {
      unchecked = 0,
      assert_   = 1,
      trap      = 2,
      assume    = 3
    };

    enum class width_policy : std::uint8_t {
      native      = 0, // field-sized, like reg_view_base
      enforce_bus = 1, // always Bus::bytes transactions (RMW for subfields)
      prefer_bus  = 2, // bus-word when it doesn't increase transaction count
      minimal_ok  = 3  // smallest allowed width >= field bytes (<= Bus::bytes); else bus-word
    };

    // Transaction widths are represented as a 4-bit mask for {1,2,4,8} bytes.
    // bit0: 1B, bit1: 2B, bit2: 4B, bit3: 8B
    using width_mask_t = std::uint8_t;

    MAD_FORCEINLINE constexpr width_mask_t mask_for_bytes(std::size_t bytes) noexcept {
      return (bytes == 1 ? width_mask_t(1u) :
             bytes == 2 ? width_mask_t(2u) :
             bytes == 4 ? width_mask_t(4u) :
             bytes == 8 ? width_mask_t(8u) : width_mask_t(0u));
    }

    inline constexpr width_mask_t width_all = width_mask_t(1u | 2u | 4u | 8u);

    template <width_mask_t ReadMask = width_all, width_mask_t WriteMask = width_all>
    struct caps {
      static constexpr width_mask_t read  = ReadMask;
      static constexpr width_mask_t write = WriteMask;
    };

    using caps_all = caps<width_all, width_all>;

    // Common "bus-only" capabilities.
    template <typename Bus>
    using caps_bus_only = caps<mask_for_bytes(Bus::bytes), mask_for_bytes(Bus::bytes)>;

    //     
    // cfg: compile-time configuration for view
    //     
    template <typename Bus,
              std::size_t BaseAlign = Bus::align,
              width_policy WidthPolicy = width_policy::native,
              align_policy AlignPolicy = align_policy::assert_,
              typename Caps = caps_all>
    struct cfg {
      using bus_type = Bus;
      using caps_type = Caps;
      static constexpr std::size_t base_align = BaseAlign;
      static constexpr width_policy width = WidthPolicy;
      static constexpr align_policy align = AlignPolicy;
      static constexpr width_mask_t read_mask  = Caps::read;
      static constexpr width_mask_t write_mask = Caps::write;
    };

    // A couple of convenience configs.
    template <typename Bus, std::size_t BaseAlign = Bus::align>
    using cfg_native = cfg<Bus, BaseAlign, width_policy::native, align_policy::assert_, caps_all>;

    template <typename Bus, std::size_t BaseAlign = Bus::align>
    using cfg_enforce_bus = cfg<Bus, BaseAlign, width_policy::enforce_bus, align_policy::assert_, caps_bus_only<Bus>>;

    template <typename Bus, std::size_t BaseAlign = Bus::align>
    using cfg_prefer_bus = cfg<Bus, BaseAlign, width_policy::prefer_bus, align_policy::assert_, caps_all>;

    //    
    // detail helpers (alignment/selection)
    //    
    namespace detail2 {

      MAD_FORCEINLINE constexpr bool is_pow2(std::size_t x) noexcept { return x && ((x & (x - 1u)) == 0u); }

      template <typename T>
      MAD_FORCEINLINE constexpr std::uintptr_t uptr(T* p) noexcept {
        return reinterpret_cast<std::uintptr_t>(p);
      }

      MAD_FORCEINLINE constexpr bool is_aligned(std::uintptr_t p, std::size_t a) noexcept {
        return (a == 0u) ? true : ((p & (a - 1u)) == 0u);
      }

      MAD_FORCEINLINE void trap_now() noexcept {
      #if defined(_MSC_VER)
        __fastfail(0);
      #elif defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
      #else
        *reinterpret_cast<volatile int*>(0) = 0;
      #endif
      }

      template <align_policy P>
      MAD_FORCEINLINE void enforce_alignment(std::uintptr_t p, std::size_t a) noexcept {
        if constexpr (P == align_policy::unchecked) {
          (void)p; (void)a;
        } else if constexpr (P == align_policy::assert_) {
          MAD_ASSERT(is_pow2(a));
          MAD_ASSERT(is_aligned(p, a));
        } else if constexpr (P == align_policy::trap) {
          if (!is_aligned(p, a)) trap_now();
        } else { // assume
          // Give the optimizer a hint. If it's false, behavior is undefined.
          #if defined(_MSC_VER)
            __assume(is_aligned(p, a));
          #elif defined(__GNUC__) || defined(__clang__)
            if (!is_aligned(p, a)) __builtin_unreachable();
          #else
            (void)p; (void)a;
          #endif
        }
      }

      // Return the maximum allowed width (<= max_bytes) from mask.
      MAD_FORCEINLINE constexpr std::size_t max_width_from_mask(width_mask_t m, std::size_t max_bytes) noexcept {
        if (max_bytes >= 8 && (m & 8u)) return 8u;
        if (max_bytes >= 4 && (m & 4u)) return 4u;
        if (max_bytes >= 2 && (m & 2u)) return 2u;
        if (max_bytes >= 1 && (m & 1u)) return 1u;
        return 0u;
      }

      // Return the minimum allowed width (>= min_bytes, <= max_bytes) from mask.
      MAD_FORCEINLINE constexpr std::size_t min_width_ge(width_mask_t m, std::size_t min_bytes, std::size_t max_bytes) noexcept {
        if (min_bytes <= 1 && max_bytes >= 1 && (m & 1u)) return 1u;
        if (min_bytes <= 2 && max_bytes >= 2 && (m & 2u)) return 2u;
        if (min_bytes <= 4 && max_bytes >= 4 && (m & 4u)) return 4u;
        if (min_bytes <= 8 && max_bytes >= 8 && (m & 8u)) return 8u;
        return 0u;
      }

      // Choose a transaction width for a byte-aligned region of N bytes starting at offset.
      //
      // - Enforces that chosen width is a power-of-two (1/2/4/8) and <= BusBytes.
      // - If the chosen width is larger than the region, the caller must do RMW.
      // - Alignment is *not* enforced here (caller decides policy).
      template <width_policy WP>
      MAD_FORCEINLINE constexpr std::size_t choose_width(std::size_t region_bytes,
                                                        std::size_t offset_bytes,
                                                        std::size_t bus_bytes,
                                                        width_mask_t mask) noexcept {
        (void)offset_bytes;
        if constexpr (WP == width_policy::enforce_bus) {
          const std::size_t w = bus_bytes;
          return ((mask & mask_for_bytes(w)) ? w : 0u);
        } else if constexpr (WP == width_policy::native) {
          // Try exact size first; if illegal, promote within bus width.
          if (mask & mask_for_bytes(region_bytes)) return region_bytes;
          const std::size_t w = min_width_ge(mask, region_bytes, bus_bytes);
          return w ? w : 0u;
        } else if constexpr (WP == width_policy::minimal_ok) {
          const std::size_t w = min_width_ge(mask, region_bytes, bus_bytes);
          return w ? w : 0u;
        } else { // prefer_bus
          // If region fits in a bus word, prefer bus word if legal.
          if (region_bytes <= bus_bytes && (mask & mask_for_bytes(bus_bytes))) return bus_bytes;
          if (mask & mask_for_bytes(region_bytes)) return region_bytes;
          const std::size_t w = min_width_ge(mask, region_bytes, bus_bytes);
          return w ? w : 0u;
        }
      }

      // Convert between host-endian word value and "LE byte-stream numeric".
      // LE byte-stream numeric means: byte at lowest address is the least significant byte.
      template <typename Word>
      MAD_FORCEINLINE constexpr std::uint64_t host_word_to_le_stream(Word w) noexcept {
        using U = std::make_unsigned_t<Word>;
        if constexpr (std::endian::native == std::endian::little) {
          return static_cast<std::uint64_t>(static_cast<U>(w));
        } else {
          return static_cast<std::uint64_t>(mad::detail::bswap(static_cast<U>(w)));
        }
      }

      template <typename Word>
      MAD_FORCEINLINE constexpr Word le_stream_to_host_word(std::uint64_t le) noexcept {
        using U = std::make_unsigned_t<Word>;
        if constexpr (std::endian::native == std::endian::little) {
          return static_cast<Word>(static_cast<U>(le));
        } else {
          return static_cast<Word>(mad::detail::bswap(static_cast<U>(le)));
        }
      }

      template <std::size_t N>
      MAD_FORCEINLINE constexpr unsigned char byte_at_native(std::uint64_t x, std::size_t i) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
          return static_cast<unsigned char>((x >> (i * 8u)) & 0xFFu);
        } else {
          return static_cast<unsigned char>((x >> ((N - 1u - i) * 8u)) & 0xFFu);
        }
      }

      template <std::size_t N>
      MAD_FORCEINLINE constexpr std::uint64_t assemble_native_from_bytes(unsigned char const* b) noexcept {
        std::uint64_t x = 0;
        if constexpr (std::endian::native == std::endian::little) {
          MAD_UNROLL
          for (std::size_t i = 0; i < N; ++i) {
            x |= (static_cast<std::uint64_t>(b[i]) << (i * 8u));
          }
        } else {
          MAD_UNROLL
          for (std::size_t i = 0; i < N; ++i) {
            x = (x << 8u) | static_cast<std::uint64_t>(b[i]);
          }
        }
        return x;
      }

      template <std::size_t WordBytes>
      MAD_FORCEINLINE constexpr unsigned char byte_from_word_native(std::uint64_t host_word, std::size_t byte_index) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
          return static_cast<unsigned char>((host_word >> (byte_index * 8u)) & 0xFFu);
        } else {
          return static_cast<unsigned char>((host_word >> ((WordBytes - 1u - byte_index) * 8u)) & 0xFFu);
        }
      }

      template <std::size_t WordBytes>
      MAD_FORCEINLINE constexpr std::uint64_t mask_region_native(std::size_t byte_index, std::size_t nbytes) noexcept {
        // nbytes in 1..WordBytes, region contiguous
        const std::uint64_t m = (nbytes == 8 ? ~0ull : ((1ull << (nbytes * 8u)) - 1ull));
        if constexpr (std::endian::native == std::endian::little) {
          return m << (byte_index * 8u);
        } else {
          return m << ((WordBytes - byte_index - nbytes) * 8u);
        }
      }

      template <std::size_t WordBytes>
      MAD_FORCEINLINE constexpr std::uint64_t shift_region_native(std::size_t byte_index, std::size_t nbytes) noexcept {
        (void)nbytes;
        if constexpr (std::endian::native == std::endian::little) {
          return static_cast<std::uint64_t>(byte_index * 8u);
        } else {
          return static_cast<std::uint64_t>((WordBytes - byte_index - nbytes) * 8u);
        }
      }

      // Bswap for a value stored in the low bits, for exactly N bytes (N in {1,2,4,8}).
      template <std::size_t N>
      MAD_FORCEINLINE constexpr std::uint64_t bswap_n(std::uint64_t x) noexcept {
        if constexpr (N == 1) return x & 0xFFu;
        if constexpr (N == 2) return static_cast<std::uint64_t>(mad::detail::bswap(static_cast<std::uint16_t>(x)));
        if constexpr (N == 4) return static_cast<std::uint64_t>(mad::detail::bswap(static_cast<std::uint32_t>(x)));
        if constexpr (N == 8) return static_cast<std::uint64_t>(mad::detail::bswap(static_cast<std::uint64_t>(x)));
        return x;
      }

      // Load/store a bus word as host integer, with alignment enforcement.
      template <typename Bus, std::size_t BaseAlign, align_policy AP>
      MAD_FORCEINLINE std::uint64_t mmio_load_bus_host(volatile std::byte const* p) noexcept {
        using Word = typename Bus::word;
        enforce_alignment<AP>(uptr(p), (BaseAlign < Bus::align ? BaseAlign : Bus::align));
#if defined(MADPACKET_STRICT_MMIO)
        // In strict mode, fall back to byte reads.
        // If MADPACKET_MMIO_HARDWIDTH is set, reject this at compile time.
        #if defined(MADPACKET_MMIO_HARDWIDTH)
          static_assert(sizeof(Word) == 0, "MADPACKET_MMIO_HARDWIDTH: typed busword access required, but MADPACKET_STRICT_MMIO forbids it");
        #endif
        return static_cast<std::uint64_t>(mad::detail::mmio_load_pod<std::make_unsigned_t<Word>, BaseAlign>(p));
#else
        // Typed bus word load (volatile).
        if constexpr (BaseAlign >= alignof(Word)) {
          if ((reinterpret_cast<std::uintptr_t>(p) & (alignof(Word) - 1u)) == 0u) {
            Word w = *reinterpret_cast<volatile Word const*>(p);
            return static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<Word>>(w));
          }
        }
        // Fallback: bytewise (host-endian assembly)
        return static_cast<std::uint64_t>(mad::detail::mmio_load_pod<std::make_unsigned_t<Word>, BaseAlign>(p));
#endif
      }

      template <typename Bus, std::size_t BaseAlign, align_policy AP>
      MAD_FORCEINLINE void mmio_store_bus_host(volatile std::byte* p, std::uint64_t host_value) noexcept {
        using Word = typename Bus::word;
        enforce_alignment<AP>(uptr(p), (BaseAlign < Bus::align ? BaseAlign : Bus::align));
#if defined(MADPACKET_STRICT_MMIO)
        #if defined(MADPACKET_MMIO_HARDWIDTH)
          static_assert(sizeof(Word) == 0, "MADPACKET_MMIO_HARDWIDTH: typed busword access required, but MADPACKET_STRICT_MMIO forbids it");
        #endif
        mad::detail::mmio_store_pod<std::make_unsigned_t<Word>, BaseAlign>(p, static_cast<std::make_unsigned_t<Word>>(host_value));
#else
        if constexpr (BaseAlign >= alignof(Word)) {
          if ((reinterpret_cast<std::uintptr_t>(p) & (alignof(Word) - 1u)) == 0u) {
            *reinterpret_cast<volatile Word*>(p) = static_cast<Word>(static_cast<std::make_unsigned_t<Word>>(host_value));
            return;
          }
        }
        mad::detail::mmio_store_pod<std::make_unsigned_t<Word>, BaseAlign>(p, static_cast<std::make_unsigned_t<Word>>(host_value));
#endif
      }

      // Load/store a bus word as LE byte-stream numeric (byte0 is LSB), with alignment enforcement.
      template <typename Bus, std::size_t BaseAlign, align_policy AP>
      MAD_FORCEINLINE std::uint64_t mmio_load_bus_le_stream(volatile std::byte const* p) noexcept {
#if defined(MADPACKET_STRICT_MMIO)
        // strict mode: existing bytewise helper already returns LE stream numeric
        enforce_alignment<AP>(uptr(p), (BaseAlign < Bus::align ? BaseAlign : Bus::align));
        return mad::detail::mmio_load_u64_le_n<Bus::bytes>(p);
#else
        using Word = typename Bus::word;
        const std::uint64_t host = mmio_load_bus_host<Bus, BaseAlign, AP>(p);
        // host is the host integer value; convert to LE stream numeric.
        return host_word_to_le_stream<Word>(static_cast<Word>(host));
#endif
      }

      template <typename Bus, std::size_t BaseAlign, align_policy AP>
      MAD_FORCEINLINE void mmio_store_bus_le_stream(volatile std::byte* p, std::uint64_t le_stream) noexcept {
#if defined(MADPACKET_STRICT_MMIO)
        enforce_alignment<AP>(uptr(p), (BaseAlign < Bus::align ? BaseAlign : Bus::align));
        mad::detail::mmio_store_u64_le_n<Bus::bytes>(p, le_stream);
#else
        using Word = typename Bus::word;
        const Word hostw = le_stream_to_host_word<Word>(le_stream);
        mmio_store_bus_host<Bus, BaseAlign, AP>(p, static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<Word>>(hostw)));
#endif
      }

      // Byte-aligned integer load via bus transactions, returns *native* value for the bytes
      // (i.e., the same as mmio_load_pod would read on this host).
      template <typename Bus, std::size_t BaseAlign, align_policy AP, std::size_t N>
      MAD_FORCEINLINE std::uint64_t load_int_bytes_native(volatile std::byte const* base, std::size_t byte_off) noexcept {
        static_assert(N == 1 || N == 2 || N == 4 || N == 8, "N must be 1/2/4/8");
        constexpr std::size_t B = Bus::bytes;
        static_assert(B == 1 || B == 2 || B == 4 || B == 8, "Bus bytes must be 1/2/4/8");

        // If the region is itself bus-word aligned and N==B, one load.
        if constexpr (N == B) {
          const volatile std::byte* p = base + byte_off;
          const std::uint64_t host = mmio_load_bus_host<Bus, BaseAlign, AP>(p);
          return host;
        } else if constexpr (N < B) {
          const std::size_t widx = byte_off / B;
          const std::size_t bin  = byte_off - (widx * B);
          const volatile std::byte* wp = base + (widx * B);
          const std::uint64_t host_word = mmio_load_bus_host<Bus, BaseAlign, AP>(wp);

          // If contained in one word, extract contiguous region.
          if (bin + N <= B) {
            const std::uint64_t sh = shift_region_native<B>(bin, N);
            const std::uint64_t m  = (N == 8 ? ~0ull : ((1ull << (N * 8u)) - 1ull)) << sh;
            return (host_word & m) >> sh;
          }

          // Spans two words: gather bytes in memory order.
          unsigned char bytes[N]{};
          const std::uint64_t host_word2 = mmio_load_bus_host<Bus, BaseAlign, AP>(wp + B);

          MAD_UNROLL
          for (std::size_t i = 0; i < N; ++i) {
            const std::size_t abs = bin + i;
            if (abs < B) bytes[i] = byte_from_word_native<B>(host_word, abs);
            else         bytes[i] = byte_from_word_native<B>(host_word2, abs - B);
          }
          return assemble_native_from_bytes<N>(bytes);
        } else { // N > B
          // Multiple bus words. Gather N bytes.
          constexpr std::size_t words = (N + B - 1u) / B;
          unsigned char bytes[N]{};

          MAD_UNROLL
          for (std::size_t wi = 0; wi < words; ++wi) {
            const std::uint64_t hw = mmio_load_bus_host<Bus, BaseAlign, AP>(base + byte_off + wi * B);
            const std::size_t take = (wi == (words - 1u)) ? (N - wi * B) : B;
            MAD_UNROLL
            for (std::size_t bi = 0; bi < take; ++bi) {
              bytes[wi * B + bi] = byte_from_word_native<B>(hw, bi);
            }
          }
          return assemble_native_from_bytes<N>(bytes);
        }
      }

      // Byte-aligned integer store via bus transactions.
      // The input value must already be *native* for the byte sequence to be stored.
      template <typename Bus, std::size_t BaseAlign, align_policy AP, std::size_t N>
      MAD_FORCEINLINE void store_int_bytes_native(volatile std::byte* base,
                                                  std::size_t byte_off,
                                                  std::uint64_t native_value,
                                                  bool rmw) noexcept {
        static_assert(N == 1 || N == 2 || N == 4 || N == 8, "N must be 1/2/4/8");
        constexpr std::size_t B = Bus::bytes;

        if constexpr (N == B) {
          volatile std::byte* p = base + byte_off;
          // full word store, no RMW needed regardless of rmw
          mmio_store_bus_host<Bus, BaseAlign, AP>(p, native_value);
          return;
        }

        // Build bytes in memory order for the native value.
        unsigned char bytes[N]{};
        MAD_UNROLL
        for (std::size_t i = 0; i < N; ++i) bytes[i] = byte_at_native<N>(native_value, i);

        if constexpr (N < B) {
          const std::size_t widx = byte_off / B;
          const std::size_t bin  = byte_off - (widx * B);
          volatile std::byte* wp = base + (widx * B);

          if (bin + N <= B) {
            // Single word RMW if required, else overwrite region in a local word and store.
            std::uint64_t host_word = rmw ? mmio_load_bus_host<Bus, BaseAlign, AP>(wp) : 0u;

            const std::uint64_t sh = shift_region_native<B>(bin, N);
            const std::uint64_t m  = mask_region_native<B>(bin, N);

            // Assemble region bits in host format (native).
            std::uint64_t region = 0u;
            if constexpr (std::endian::native == std::endian::little) {
              MAD_UNROLL
              for (std::size_t i = 0; i < N; ++i) region |= (static_cast<std::uint64_t>(bytes[i]) << (i * 8u));
            } else {
              MAD_UNROLL
              for (std::size_t i = 0; i < N; ++i) region = (region << 8u) | static_cast<std::uint64_t>(bytes[i]);
            }

            host_word = (host_word & ~m) | ((region << sh) & m);
            mmio_store_bus_host<Bus, BaseAlign, AP>(wp, host_word);
            return;
          }

          // Spans two words: perform per-word updates.
          volatile std::byte* wp2 = wp + B;

          const std::size_t first_take = B - bin;
          const std::size_t second_take = N - first_take;

          std::uint64_t w0 = rmw ? mmio_load_bus_host<Bus, BaseAlign, AP>(wp)  : 0u;
          std::uint64_t w1 = rmw ? mmio_load_bus_host<Bus, BaseAlign, AP>(wp2) : 0u;

          const std::uint64_t m0 = mask_region_native<B>(bin, first_take);
          const std::uint64_t m1 = mask_region_native<B>(0u, second_take);

          // region0 uses bytes[0..first_take)
          std::uint64_t r0 = 0u;
          if constexpr (std::endian::native == std::endian::little) {
            MAD_UNROLL
            for (std::size_t i = 0; i < first_take; ++i) r0 |= (static_cast<std::uint64_t>(bytes[i]) << (i * 8u));
          } else {
            MAD_UNROLL
            for (std::size_t i = 0; i < first_take; ++i) r0 = (r0 << 8u) | static_cast<std::uint64_t>(bytes[i]);
          }

          // region1 uses bytes[first_take..)
          std::uint64_t r1 = 0u;
          if constexpr (std::endian::native == std::endian::little) {
            MAD_UNROLL
            for (std::size_t i = 0; i < second_take; ++i) r1 |= (static_cast<std::uint64_t>(bytes[first_take + i]) << (i * 8u));
          } else {
            MAD_UNROLL
            for (std::size_t i = 0; i < second_take; ++i) r1 = (r1 << 8u) | static_cast<std::uint64_t>(bytes[first_take + i]);
          }

          const std::uint64_t sh0 = shift_region_native<B>(bin, first_take);
          const std::uint64_t sh1 = shift_region_native<B>(0u, second_take);

          w0 = (w0 & ~m0) | ((r0 << sh0) & m0);
          w1 = (w1 & ~m1) | ((r1 << sh1) & m1);

          mmio_store_bus_host<Bus, BaseAlign, AP>(wp,  w0);
          mmio_store_bus_host<Bus, BaseAlign, AP>(wp2, w1);
          return;
        } else { // N > B
          // Multiple full bus words. We do per-word stores; if rmw==true, only needed
          // when N is not a multiple of B, and we must preserve the tail word bytes.
          constexpr std::size_t words = (N + B - 1u) / B;

          MAD_UNROLL
          for (std::size_t wi = 0; wi < words; ++wi) {
            volatile std::byte* wp = base + byte_off + wi * B;
            const std::size_t take = (wi == (words - 1u)) ? (N - wi * B) : B;

            std::uint64_t w = (rmw && take != B) ? mmio_load_bus_host<Bus, BaseAlign, AP>(wp) : 0u;

            // Build the new word bytes (first `take` bytes overwrite, remainder preserved if rmw).
            unsigned char wb[B]{};
            if (rmw && take != B) {
              // Extract current word bytes in memory order into wb.
              const std::uint64_t hw = w;
              MAD_UNROLL
              for (std::size_t bi = 0; bi < B; ++bi) wb[bi] = byte_from_word_native<B>(hw, bi);
            }

            MAD_UNROLL
            for (std::size_t bi = 0; bi < take; ++bi) wb[bi] = bytes[wi * B + bi];

            // Assemble wb into host word numeric.
            std::uint64_t neww = assemble_native_from_bytes<B>(wb);
            mmio_store_bus_host<Bus, BaseAlign, AP>(wp, neww);
          }
          return;
        }
      }

    } // namespace detail2

    //    
    // layout validation helpers (compile-time, for sanity checks)
    //    
    template <typename Packet, typename Cfg>
    struct layout_info {
      using Bus = typename Cfg::bus_type;
      static constexpr std::size_t bus_bytes = Bus::bytes;
      static constexpr std::size_t bus_align = Bus::align;
      static constexpr std::size_t base_align = Cfg::base_align;

      // If you request enforce_bus, BaseAlign must support bus alignment.
      static constexpr bool base_align_ok =
        (Cfg::width != width_policy::enforce_bus) ? true : (base_align >= bus_align);

      // Determine if every byte-aligned integer field is fully contained in a single bus word
      // when using bus-word transactions. This can matter for bridges where multi-word
      // sequences must be avoided.
      template <std::size_t I>
      static constexpr bool int_fits_in_one_word() noexcept {
        using F = typename Packet::template field_t<I>;
        if constexpr (F::kind != field_kind::int_bits) return true;
        constexpr std::size_t bit_off = mad::detail::field_bit_offset_v<Packet, I>;
        constexpr std::size_t shift = bit_off & 7u;
        if constexpr (shift != 0) return true; // bitfield handled separately
        constexpr std::size_t bits = F::bits;
        if constexpr (!(bits == 8 || bits == 16 || bits == 32 || bits == 64)) return true;
        constexpr std::size_t bytes = bits / 8u;
        constexpr std::size_t byte_off = bit_off >> 3;
        if constexpr (bytes <= bus_bytes) {
          const std::size_t bin = byte_off % bus_bytes;
          return (bin + bytes) <= bus_bytes;
        } else {
          return false; // spans multiple words (e.g., 64b on 32b bus)
        }
      }

      template <std::size_t... Is>
      static constexpr bool all_ints_one_word_impl(std::index_sequence<Is...>) noexcept {
        return (true && ... && int_fits_in_one_word<Is>());
      }

      static constexpr bool all_ints_one_bus_word =
        all_ints_one_word_impl(std::make_index_sequence<Packet::field_count>{});

      // Quick estimate of worst-case transactions for a single get/set of a given field index.
      template <std::size_t I>
      static constexpr std::size_t worst_case_transactions() noexcept {
        using F = typename Packet::template field_t<I>;
        if constexpr (F::kind == field_kind::bytes) {
          // bytes fields are not transacted by this API (return ref)
          return 0;
        } else if constexpr (F::kind == field_kind::subpacket) {
          return 0;
        } else if constexpr (F::kind == field_kind::pad) {
          return 0;
        } else {
          constexpr std::size_t bit_off = mad::detail::field_bit_offset_v<Packet, I>;
          if constexpr (F::kind == field_kind::int_bits) {
            constexpr std::size_t bits = F::bits;
            constexpr std::size_t shift = bit_off & 7u;
            if constexpr (shift == 0 && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
              constexpr std::size_t bytes = bits / 8u;
              if constexpr (Cfg::width == width_policy::enforce_bus) {
                // one or more bus words
                return (bytes + bus_bytes - 1u) / bus_bytes;
              } else {
                // field-sized access or promoted
                const std::size_t w = detail2::choose_width<Cfg::width>(bytes, (bit_off >> 3), bus_bytes,
                                                                       Cfg::read_mask | Cfg::write_mask);
                if constexpr (w == 0) return (bytes + bus_bytes - 1u) / bus_bytes;
                return (bytes + w - 1u) / w;
              }
            } else {
              // bitfield: at most 2 words when bus >= 16 and field <=64; else fallback
              return 2;
            }
          }
          return 0;
        }
      }
    };

    template <typename Packet, typename Cfg>
    inline constexpr bool layout_ok_v = layout_info<Packet, Cfg>::base_align_ok;

    template <typename Packet, typename Cfg>
    MAD_FORCEINLINE constexpr void static_validate() noexcept {
      static_assert(layout_ok_v<Packet, Cfg>, "mad::reg::xview: BaseAlign insufficient for requested bus enforcement");
    }

       // xview: policy-driven MMIO
       namespace detail2 {

      template <typename Packet, bool Mutable, std::size_t I, typename Cfg>
      MAD_FORCEINLINE auto xget_impl(typename mad::detail::vptr_type<Packet, Mutable>::type base) noexcept {
        using Bus = typename Cfg::bus_type;
        using F = typename Packet::template field_t<I>;
        constexpr std::size_t bit_off = mad::detail::field_bit_offset_v<Packet, I>;

        if constexpr (F::kind == field_kind::int_bits) {
          constexpr std::size_t bits = F::bits;
          constexpr std::size_t shift = bit_off & 7u;
          constexpr std::size_t byte_off = bit_off >> 3;

          // Byte-aligned 8/16/32/64 fast path (policy-driven width selection).
          if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
            constexpr std::size_t N = bits / 8u;

            // Decide transaction width.
            constexpr std::size_t chosen = choose_width<Cfg::width>(N, byte_off, Bus::bytes, Cfg::read_mask);

            // If native policy and exact-sized access is allowed, we can reuse original mmio_load_pod.
            if constexpr (Cfg::width == width_policy::native && chosen == N && (N == 1 || N == 2 || N == 4 || N == 8)) {
              using U = mad::detail::uint_for_bits_t<bits>;
              U x = mad::detail::mmio_load_pod<U, Cfg::base_align>(base + byte_off);
              if constexpr (!mad::detail::is_native_endian<typename F::endian>) {
                if constexpr (mad::detail::need_bswap<typename F::endian>) x = mad::detail::bswap(x);
              }
              if constexpr (F::is_signed) {
                using S = mad::detail::int_for_bits_t<bits>;
                return static_cast<std::int64_t>(static_cast<S>(x));
              } else {
                return static_cast<std::uint64_t>(x);
              }
            }

            // Otherwise, perform bus-style reads and assemble N bytes as native.
            // (If chosen==0, fall back to bus-word.)
            const std::uint64_t native = load_int_bytes_native<Bus, Cfg::base_align, Cfg::align, N>(base, byte_off);

            std::uint64_t v = native;
            if constexpr (!mad::detail::is_native_endian<typename F::endian>) {
              if constexpr (mad::detail::need_bswap<typename F::endian>) v = bswap_n<N>(v);
            }

            if constexpr (F::is_signed) {
              // sign extend from bits
              return mad::detail::sign_extend<bits, true>(v);
            } else {
              return v & mad::detail::mask64<bits>;
            }
          } else {

          // Bitfield / unaligned integer bits (endian must be native).
          static_assert(mad::detail::is_native_endian<typename F::endian>, "non-byte-multiple fields cannot specify endianness");

          constexpr std::size_t bus_bits = Bus::bits;
          constexpr std::size_t bus_bytes = Bus::bytes;
          constexpr std::size_t word_idx = bit_off / bus_bits;
          constexpr std::size_t bit_in_word = bit_off - (word_idx * bus_bits);
          constexpr bool fits_one = (bit_in_word + bits) <= bus_bits;

          if constexpr (fits_one && (bus_bits <= 64) && (bus_bits % 8 == 0)) {
            const volatile std::byte* wp = base + (word_idx * bus_bytes);

            // Prefer bus-word transaction (LE-stream) for bit extraction.
            const std::uint64_t w = mmio_load_bus_le_stream<Bus, Cfg::base_align, Cfg::align>(wp);
            const std::uint64_t m = (mad::detail::mask64<bits> << bit_in_word);
            const std::uint64_t x = (w & m) >> bit_in_word;
            return mad::detail::sign_extend<bits, F::is_signed>(x);
          } else {
            // Slow fallback: byte window RMW.
            std::uint64_t x = mad::detail::mmio_read_bits_le<bit_off, bits>(base);
            return mad::detail::sign_extend<bits, F::is_signed>(x);
          }
          }

        } else if constexpr (F::kind == field_kind::bytes) {
          constexpr std::size_t byte = bit_off >> 3;
          return mad::detail::mmio_bytes_ref<F::bytes, Mutable>{ base + byte };
        } else {
          return;
        }
      }

      template <typename Packet, bool Mutable, std::size_t I, typename V, typename Cfg>
      MAD_FORCEINLINE void xset_impl(typename mad::detail::vptr_type<Packet, Mutable>::type base, V&& v) noexcept {
        static_assert(Mutable, "attempting to set on const xview");
        using Bus = typename Cfg::bus_type;
        using F = typename Packet::template field_t<I>;
        constexpr std::size_t bit_off = mad::detail::field_bit_offset_v<Packet, I>;

        if constexpr (F::kind == field_kind::int_bits) {
          constexpr std::size_t bits = F::bits;
          constexpr std::size_t shift = bit_off & 7u;
          constexpr std::size_t byte_off = bit_off >> 3;

          if constexpr ((shift == 0) && (bits == 8 || bits == 16 || bits == 32 || bits == 64)) {
            constexpr std::size_t N = bits / 8u;
            const std::uint64_t in64 = mad::detail::to_u64(std::forward<V>(v)) & mad::detail::mask64<bits>;

            // Apply endian conversion to the stored native representation.
            std::uint64_t native = in64;
            if constexpr (!mad::detail::is_native_endian<typename F::endian>) {
              if constexpr (mad::detail::need_bswap<typename F::endian>) native = bswap_n<N>(native);
            }

            // Choose write width.
            constexpr std::size_t chosen = choose_width<Cfg::width>(N, byte_off, Bus::bytes, Cfg::write_mask);

            // If native policy and exact store allowed, use original mmio_store_pod.
            if constexpr (Cfg::width == width_policy::native && chosen == N && (N == 1 || N == 2 || N == 4 || N == 8)) {
              using U = mad::detail::uint_for_bits_t<bits>;
              U x = static_cast<U>(native);
              MAD_MMIO_BARRIER();
              mad::detail::mmio_store_pod<U, Cfg::base_align>(const_cast<volatile std::byte*>(base + byte_off), x);
              MAD_MMIO_BARRIER();
              return;
            }

            // Otherwise, use bus transactions, preserving unrelated bytes (RMW) if promoted.
            // We must RMW if chosen > N (writing wider than the field) or if N is not an exact bus multiple.
            const bool rmw = (Cfg::width == width_policy::enforce_bus) ? (N != Bus::bytes)
                          : ((chosen != 0u && chosen > N) || (N % Bus::bytes != 0u));

            MAD_MMIO_BARRIER();
            store_int_bytes_native<Bus, Cfg::base_align, Cfg::align, N>(const_cast<volatile std::byte*>(base), byte_off, native, rmw);
            MAD_MMIO_BARRIER();
            return;
          } else {

          static_assert(mad::detail::is_native_endian<typename F::endian>, "non-byte-multiple fields cannot specify endianness");

          constexpr std::size_t bus_bits = Bus::bits;
          constexpr std::size_t bus_bytes = Bus::bytes;
          constexpr std::size_t word_idx = bit_off / bus_bits;
          constexpr std::size_t bit_in_word = bit_off - (word_idx * bus_bits);
          constexpr bool fits_one = (bit_in_word + bits) <= bus_bits;

          const std::uint64_t value = mad::detail::to_u64(std::forward<V>(v)) & mad::detail::mask64<bits>;

          if constexpr (fits_one && (bus_bits <= 64) && (bus_bits % 8 == 0)) {
            volatile std::byte* wp = const_cast<volatile std::byte*>(base + (word_idx * bus_bytes));

            std::uint64_t w = mmio_load_bus_le_stream<Bus, Cfg::base_align, Cfg::align>(wp);
            const std::uint64_t m = (mad::detail::mask64<bits> << bit_in_word);
            w = (w & ~m) | (value << bit_in_word);

            MAD_MMIO_BARRIER();
            mmio_store_bus_le_stream<Bus, Cfg::base_align, Cfg::align>(wp, w);
            MAD_MMIO_BARRIER();
          } else {
            // Slow fallback: byte window RMW.
            MAD_MMIO_BARRIER();
            mad::detail::mmio_write_bits_le<bit_off, bits>(const_cast<volatile std::byte*>(base), value);
            MAD_MMIO_BARRIER();
          }
          }

        } else if constexpr (F::kind == field_kind::bytes) {
          static_assert(sizeof(F) == 0, "bytes field: write via get<name>().data()/size() and volatile stores");
        } else if constexpr (F::kind == field_kind::subpacket) {
          static_assert(sizeof(F) == 0, "subpacket field: assign via nested reg view");
        } else {
          // pad
        }
      }

    } // namespace detail2

    template <typename Packet, bool Mutable, typename Cfg>
    class xview_base {
      mad::detail::vptr_type_t<Packet, Mutable> base_{};
      MAD_FORCEINLINE static constexpr void validate_() noexcept { static_validate<Packet, Cfg>(); }
    public:
      using packet_type = Packet;
      using cfg_type = Cfg;
      using bus_type = typename Cfg::bus_type;
      using pointer = mad::detail::vptr_type_t<Packet, Mutable>;
      static constexpr std::size_t base_align = Cfg::base_align;

      MAD_FORCEINLINE constexpr xview_base() noexcept { validate_(); }
      MAD_FORCEINLINE constexpr explicit xview_base(pointer p) noexcept : base_(p) { validate_(); }

      MAD_FORCEINLINE constexpr pointer data() const noexcept { return base_; }
      static constexpr std::size_t size_bytes() noexcept { return Packet::total_bytes; }
      static constexpr std::size_t size_bits() noexcept { return Packet::total_bits; }

      template <fixed_string Name>
      MAD_FORCEINLINE auto get() const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot get a pad field");

        if constexpr (F::kind == field_kind::subpacket) {
          constexpr std::size_t bit_off = mad::detail::field_bit_offset_v<Packet, idx>;
          constexpr std::size_t byte = bit_off >> 3;
          using Sub = typename F::packet;
          return xview_base<Sub, Mutable, Cfg>(base_ + byte);
        } else {
          return detail2::xget_impl<Packet, Mutable, idx, Cfg>(base_);
        }
      }

      template <fixed_string Name, typename V>
      MAD_FORCEINLINE void set(V&& v) const noexcept {
        constexpr std::size_t idx = Packet::template index_of<Name>;
        static_assert(idx != static_cast<std::size_t>(-1), "field name not found");
        using F = typename Packet::template field_t<idx>;
        static_assert(F::kind != field_kind::pad, "cannot set a pad field");
        detail2::xset_impl<Packet, Mutable, idx, V, Cfg>(base_, std::forward<V>(v));
      }

      template <std::size_t I>
      MAD_FORCEINLINE auto get_i() const noexcept {
        static_assert(I < Packet::field_count);
        using F = typename Packet::template field_t<I>;
        static_assert(F::kind != field_kind::pad, "cannot get a pad field");
        if constexpr (F::kind == field_kind::subpacket) {
          constexpr std::size_t bit_off = mad::detail::field_bit_offset_v<Packet, I>;
          constexpr std::size_t byte = bit_off >> 3;
          using Sub = typename F::packet;
          return xview_base<Sub, Mutable, Cfg>(base_ + byte);
        } else {
          return detail2::xget_impl<Packet, Mutable, I, Cfg>(base_);
        }
      }

      template <std::size_t I, typename V>
      MAD_FORCEINLINE void set_i(V&& v) const noexcept {
        static_assert(I < Packet::field_count);
        using F = typename Packet::template field_t<I>;
        static_assert(F::kind != field_kind::pad, "cannot set a pad field");
        detail2::xset_impl<Packet, Mutable, I, V, Cfg>(base_, std::forward<V>(v));
      }
    };

    template <typename Packet, typename Cfg = cfg_native<bus32>>
    using xview = xview_base<Packet, true, Cfg>;

    template <typename Packet, typename Cfg = cfg_native<bus32>>
    using xcview = xview_base<Packet, false, Cfg>;

    template <typename Packet, typename Cfg = cfg_native<bus32>>
    MAD_FORCEINLINE constexpr xcview<Packet, Cfg> make_xview(volatile void const* addr) noexcept {
      using Bus = typename Cfg::bus_type;
      auto p = reinterpret_cast<volatile std::byte const*>(addr);
      detail2::enforce_alignment<Cfg::align>(detail2::uptr(p), Cfg::base_align);
      if constexpr (Cfg::width == width_policy::enforce_bus) {
        detail2::enforce_alignment<Cfg::align>(detail2::uptr(p), Bus::align);
      }
      return xcview<Packet, Cfg>(p);
    }

    template <typename Packet, typename Cfg = cfg_native<bus32>>
    MAD_FORCEINLINE constexpr xview<Packet, Cfg> make_xview(volatile void* addr) noexcept {
      using Bus = typename Cfg::bus_type;
      auto p = reinterpret_cast<volatile std::byte*>(addr);
      detail2::enforce_alignment<Cfg::align>(detail2::uptr(p), Cfg::base_align);
      if constexpr (Cfg::width == width_policy::enforce_bus) {
        detail2::enforce_alignment<Cfg::align>(detail2::uptr(p), Bus::align);
      }
      return xview<Packet, Cfg>(p);
    }

    // Alternate names (explicit constness).
    template <typename Packet, typename Cfg = cfg_native<bus32>>
    MAD_FORCEINLINE constexpr xcview<Packet, Cfg> make_xcview(volatile void const* addr) noexcept {
      return make_xview<Packet, Cfg>(addr);
    }

    template <typename Packet, typename Cfg = cfg_native<bus32>>
    MAD_FORCEINLINE constexpr xview<Packet, Cfg> make_xview_mut(volatile void* addr) noexcept {
      return make_xview<Packet, Cfg>(addr);
    }

    // Register block utilities
    //
    // Common embedded pattern:
    //   base + i*STRIDE gives you the i-th peripheral instance / channel.
    //
    // block_view provides O(1) indexed access to xview / xcview instances.
    //
    template <typename Packet, bool Mutable, typename Cfg, std::size_t StrideBytes>
    class block_view_base {
      using pointer = mad::detail::vptr_type_t<Packet, Mutable>;
      pointer base_{};
      std::size_t count_{};
    public:
      using packet_type = Packet;
      using cfg_type = Cfg;
      static constexpr std::size_t stride = StrideBytes;

      MAD_FORCEINLINE constexpr block_view_base() noexcept = default;
      MAD_FORCEINLINE constexpr block_view_base(pointer p, std::size_t count) noexcept : base_(p), count_(count) {}

      MAD_FORCEINLINE constexpr std::size_t size() const noexcept { return count_; }

      MAD_FORCEINLINE auto operator[](std::size_t i) const noexcept {
        MAD_ASSERT(i < count_);
        return xview_base<Packet, Mutable, Cfg>(base_ + i * StrideBytes);
      }
    };

    template <typename Packet, typename Cfg = cfg_native<bus32>, std::size_t StrideBytes = Packet::total_bytes>
    using block_view = block_view_base<Packet, true, Cfg, StrideBytes>;

    template <typename Packet, typename Cfg = cfg_native<bus32>, std::size_t StrideBytes = Packet::total_bytes>
    using cblock_view = block_view_base<Packet, false, Cfg, StrideBytes>;

    template <typename Packet, typename Cfg = cfg_native<bus32>, std::size_t StrideBytes = Packet::total_bytes>
    MAD_FORCEINLINE constexpr cblock_view<Packet, Cfg, StrideBytes> make_block_view(volatile void const* addr, std::size_t count) noexcept {
      auto p = reinterpret_cast<volatile std::byte const*>(addr);
      detail2::enforce_alignment<Cfg::align>(detail2::uptr(p), Cfg::base_align);
      return cblock_view<Packet, Cfg, StrideBytes>(p, count);
    }

    template <typename Packet, typename Cfg = cfg_native<bus32>, std::size_t StrideBytes = Packet::total_bytes>
    MAD_FORCEINLINE constexpr block_view<Packet, Cfg, StrideBytes> make_block_view(volatile void* addr, std::size_t count) noexcept {
      auto p = reinterpret_cast<volatile std::byte*>(addr);
      detail2::enforce_alignment<Cfg::align>(detail2::uptr(p), Cfg::base_align);
      return block_view<Packet, Cfg, StrideBytes>(p, count);
    }

    // Compile-time "strictness" helpers for width+alignment
    //
    // Use these for targets where violating constraints is a hard fault.
    //
    // Example:
    //   using C = mad::reg::cfg_enforce_bus<mad::reg::bus32>;
    //   mad::reg::static_validate<MyRegs, C>();
    //   static_assert(mad::reg::layout_info<MyRegs, C>::all_ints_one_bus_word);
    //
    template <typename Packet, typename Cfg = cfg_native<bus32>>
    struct strict {
      static constexpr void validate() noexcept { static_validate<Packet, Cfg>(); }
      static constexpr bool all_ints_one_word = layout_info<Packet, Cfg>::all_ints_one_bus_word;
    };

  } // namespace reg

} // namespace mad

#endif // MADPACKET_HPP_INCLUDED
