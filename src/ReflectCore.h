/*
ReflectCore - shared reflection core of ReflectJSON, ReflectINI and ReflectState (Arduino / C++17)

Copyright (c) 2026 Michael

Commercial licensing available under separate written agreement.

See LICENSE and COMMERCIAL.md.

Contains only what the Reflect libraries need in common: REFLECT / MEMBER / MEMBER_AS / COMMENT,
FieldRef, fn() / fx(), the type traits (optional, numeric wrappers), shared number helpers,
cppTypeName() and the number formatting used by ReflectJSON and ReflectINI
(reflectFormatInt / reflectFormatReal: identical output in JSON and INI).

Format-specific code lives in ReflectJSON_tools.h / ReflectINI_tools.h.
State-specific code lives in ReflectState.h.

IMPORTANT: ReflectJSON, ReflectINI and ReflectState each ship an identical copy of this file.
The copies have different paths, so #pragma once cannot detect them as the same file:
a classic include guard is used.

REFLECTCORE_VERSION must be increased on every change (in BOTH places in this file:
the check before the guard and the #define).

Every copy checks its own version against the copy included first.
ReflectJSON_tools.h, ReflectINI_tools.h and ReflectState.h also check the core version
they were written for.

Mixing libraries that ship different ReflectCore versions in one sketch therefore gives
a compile error instead of silently using the first included copy.
*/

// Outside the include guard: every copy compares ITS OWN version with the copy that was included first.
// (Inside the guard the check would never run for the second copy.)

#if defined(REFLECTCORE_VERSION) && (REFLECTCORE_VERSION != 6)
  #error "Two different ReflectCore.h versions in one build: update ReflectJSON and ReflectINI to the same ReflectCore.h"
#endif

#ifndef REFLECTCORE_H
#define REFLECTCORE_H

#define REFLECTCORE_VERSION 6

#define PM5_PRAGMA(x) _Pragma(#x)

#if defined(_MSC_VER)
  #define REGION(name)   PM5_PRAGMA(region name)
  #define ENDREGION()    PM5_PRAGMA(endregion)
#else
  #define REGION(name)   /* nothing */
  #define ENDREGION()    /* nothing */
#endif

#if defined(ESP8266) || defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_SAM)  || \
    defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_STM32)
  #error "ReflectJSON/ReflectINI require a modern C++17-capable Arduino core (ESP32, RP2350/RP2040, Teensy 4.x, Arduino UNO R4 Minima/WiFi). ESP8266, AVR, SAM/SAMD and STM32 Arduino cores are not supported."
#endif

#include <type_traits>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <climits>
#include <limits>
#include <cmath>
#include <utility>
#include <new>

#if defined(__has_include)
  #if __has_include(<optional>)
    #include <optional>
    #define JSON_HAS_OPTIONAL 1
  #endif
#endif

#ifdef ARDUINO
#include <Arduino.h>
#endif

// Maximum length of a reflection key (MEMBER / MEMBER_AS), checked at compile time by litLen().
// JSON_MAX_KEY_LEN (ReflectJSON_tools.h) and INI_MAX_KEY_LEN (ReflectINI_tools.h) are defined from it.
#ifndef REFLECT_MAX_KEY_LEN
#define REFLECT_MAX_KEY_LEN 32
#endif
constexpr uint8_t MAX_FLOAT_PRECISION = 8;

#if defined(__GNUC__)
    #define FORCE_INLINE __attribute__((always_inline)) inline
    #define REFLECT_NOINLINE __attribute__((noinline))
#else
    #define FORCE_INLINE inline
    #define REFLECT_NOINLINE
#endif


REGION(typen)
  template<typename T>
  class is_reflectable {
    private:
      template<typename U>
      static auto test(int) -> decltype(
          std::declval<const U&>().reflect( std::declval<void(*)(const char*, int)>() ),
          std::true_type{}
      );

      template<typename>
      static std::false_type test(...);

    public:
        static constexpr bool value = decltype(test<T>(0))::value;
  };

  template<typename T>
  constexpr bool is_reflectable_v = is_reflectable<T>::value;

  template<typename>
  inline constexpr bool always_false = false;

  template<typename T>
  using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;
ENDREGION()

REGION(REFLECT)
  template<typename T>
  struct FieldRef {
      const char *key;
      uint8_t keyLen;
      T value;
  };

  template<size_t N>
  constexpr uint8_t litLen(const char (&)[N]) {
    static_assert(N - 1 <= REFLECT_MAX_KEY_LEN, "reflection key exceeds REFLECT_MAX_KEY_LEN (= JSON_MAX_KEY_LEN / INI_MAX_KEY_LEN)");
    return uint8_t(N - 1);
  }

  #define MEMBER_GET_MACRO(_1,_2,NAME,...) NAME
  /**
  * @brief Declares a reflected struct field.
  *
  * Usage:
  *   MEMBER(field)
  *   MEMBER(field, expression)         //expression = fx() and fn()
  *
  * Expands to a FieldRef containing the key name, key length,
  * and a reference/expression to the field value.
  */
  #define MEMBER(...) MEMBER_GET_MACRO(__VA_ARGS__, MEMBER2, MEMBER1)(__VA_ARGS__)

  #define MEMBER1(field) FieldRef<decltype((this->field))>{ #field, litLen(#field), (this->field) }
  #define MEMBER2(field, expr) FieldRef<decltype(expr)>{ #field, litLen(#field), (expr) }

  /**
  * @brief Declares a reflected struct entry with a custom key (JSON key / INI key or section name).
  *
  * Usage:
  *   MEMBER_AS(key, expr)
  *
  * The second argument may be any valid expression referring to a value
  * (e.g. a struct member, an indexed element, or a computed wrapper).
  *
  * Expands to a FieldRef containing the provided key, its length,
  * and the evaluated expression.
  */
  #define MEMBER_AS(key, expr) FieldRef<decltype((expr))>{ key, litLen(key), (expr) }

  /**
  * @brief A comment inside REFLECT(...), e.g. COMMENT("calibration value a").
  *
  * INIwriter writes it as "; text" line(s) in front of the next MEMBER: in front of the key, or in front of
  * the [section] header if the next member is a struct (array of structs: in front of [name.0]). A COMMENT
  * without a following MEMBER is written after the last key of the struct. If the member is omitted
  * (empty std::optional, nullptr), its comment is omitted too. "\n" in the text starts a new
  * comment line. INIreader and ReflectJSON (reader and writer) ignore comments.
  *
  * Only string literals are accepted (the text must live as long as the program).
  */
  struct ReflectComment {
    const char *text;
  };
  #define COMMENT(text) ReflectComment{ "" text }

  template<typename F>
  inline constexpr bool is_reflect_comment_v = std::is_same_v<remove_cvref_t<F>, ReflectComment>;

  // Number of real members (without COMMENT) in a REFLECT pack
  template<typename... Fs>
  inline constexpr size_t reflect_member_count_v = (size_t(0) + ... + size_t(!is_reflect_comment_v<Fs>));
  /**
  * @brief Enables reflection for a struct.
  *
  * The reflect() function is used by all readers and writers (JSONreader, JSONwriter,
  * INIreader, INIwriter) to iterate over all reflected members.
  * Defines two reflect() overloads (const and non-const)
  * that forward all reflected members to the provided callable.
  *
  * The visitor parameter has an unusual name (jsonVisitor_) so that it can
  * never collide with a struct member inside MEMBER(field, expression).
  *
  * Usage:
  *   struct Foo {
  *     int a;
  *     float b;
  *     bool c;
  *     REFLECT(
  *       MEMBER(a),                    //field (int a) with key "a"
  *       MEMBER(b),                    //field (float b) with key "b"
  *       MEMBER_AS("custom key", c)    //field (bool c) with key "custom key"
  *     )
  *   };
  */
  #define REFLECT(...) \
  template<typename JsonVisitorT_> \
  void reflect(JsonVisitorT_&& jsonVisitor_) const { \
    jsonVisitor_(__VA_ARGS__); \
  } \
  template<typename JsonVisitorT_> \
  void reflect(JsonVisitorT_&& jsonVisitor_) { \
    jsonVisitor_(__VA_ARGS__); \
  }
ENDREGION()

REGION(numbers)
  /**
  * @brief Numeric wrapper for floating-point values with configurable precision.
  *
  * JsonNumber is used to control how floating-point values are
  * written (number of decimal places) by JSONwriter and INIwriter.
  * The name is kept for compatibility; it is not JSON-specific.
  *
  * Typically constructed via fn():
  *   fn(value, precision)
  *
  * This wrapper does not own the value; it stores a reference.
  */
  template<typename T>
  struct JsonNumber {
    static_assert(std::is_floating_point_v<remove_cvref_t<T>>, "JsonNumber is only for floating point types");

    T& value;             ///< referenced numeric value
    uint8_t precision;    ///< number of decimal places
  };
  template<typename T>
  JsonNumber(T&, uint8_t) -> JsonNumber<T>;

  /**
  * @brief Creates a JsonNumber wrapper with the given precision.
  *
  * Usage:
  *   MEMBER(field, fn(field, 2))
  *
  * Precision values above MAX_FLOAT_PRECISION (8) are clamped.
  */
  template<typename T>
  FORCE_INLINE JsonNumber<T> fn(T &v, uint8_t precision) {
    if(precision > MAX_FLOAT_PRECISION){
      precision = MAX_FLOAT_PRECISION;
    }
    return { v, precision };
  }


  /**
  * @brief Fixed-point numeric wrapper for floating-point values.
  *
  * JsonFixed represents floating-point values as fixed-point numbers
  * with a fixed number of decimal places. In JSON and INI the value is
  * written as an integer (value * 10^precision).
  * The name is kept for compatibility; it is not JSON-specific.
  *
  * Typically constructed via fx():
  *   fx(value, precision)
  *
  * This wrapper does not own the value; it stores a reference.
  */
  template<typename T>
  struct JsonFixed {
    static_assert(std::is_floating_point_v<remove_cvref_t<T>>, "JsonFixed is only for floating point types");

    T& value;             ///< referenced numeric value
    uint8_t precision;    ///< fixed decimal places
  };
  template<typename T>
  JsonFixed(T&, uint8_t) -> JsonFixed<T>;

  /**
  * @brief Creates a JsonFixed wrapper with the given precision.
  *
  * Usage:
  *   MEMBER(field, fx(field, 3))
  *
  * Precision values above MAX_FLOAT_PRECISION (8) are clamped.
  */
  template<typename T>
  FORCE_INLINE JsonFixed<T> fx(T &v, uint8_t precision) {
    if(precision > MAX_FLOAT_PRECISION){
      precision = MAX_FLOAT_PRECISION;
    }
    return { v, precision };
  }

  /**
  * @brief Returns the default number of decimal places written for a numeric type.
  *
  * Used when no explicit precision is specified.
  *
  * float  -> 2
  * double -> 4
  */
  template<typename T>
  constexpr uint8_t defaultPrecision() {
    using U = remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, float>)  return 2;
    if constexpr (std::is_same_v<U, double>) return 4;
    return 0;
  }

  template<typename>
  struct is_json_fixed : std::false_type {};
  template<typename T>
  struct is_json_fixed<JsonFixed<T>> : std::true_type {};

  // std::optional<T> = nullable type (like "T?" in Kotlin)
  template<typename T>
  struct is_json_optional : std::false_type {};
#ifdef JSON_HAS_OPTIONAL
  template<typename T>
  struct is_json_optional<std::optional<T>> : std::true_type {};
#endif
  template<typename T>
  inline constexpr bool is_json_optional_v = is_json_optional<remove_cvref_t<T>>::value;

  template<typename T>
  struct json_optional_value { using type = T; };
#ifdef JSON_HAS_OPTIONAL
  template<typename T>
  struct json_optional_value<std::optional<T>> { using type = T; };
#endif

  template<typename T>
  inline constexpr bool is_json_fixed_v = is_json_fixed<T>::value;

  template<typename>
  struct is_json_number : std::false_type {};
  template<typename T>
  struct is_json_number<JsonNumber<T>> : std::true_type {};
  template<typename T>
  inline constexpr bool is_json_number_v = is_json_number<T>::value;

  template<typename T>
  struct unwrap_numeric {
      using type = remove_cvref_t<T>;
  };

  template<typename T>
  struct unwrap_numeric<JsonFixed<T>>  { using type = remove_cvref_t<T>; };

  template<typename T>
  struct unwrap_numeric<JsonNumber<T>> { using type = remove_cvref_t<T>; };

  template<typename T>
  struct unwrap_numeric<FieldRef<T>>   { using type = typename unwrap_numeric<T>::type; };

  template<typename T>
  using unwrap_numeric_t = typename unwrap_numeric<T>::type;
ENDREGION()

REGION(corehelper)
  // Shared number helpers (ReflectJSON reader/writer and ReflectINI reader/writer)
  #define JSON_IS_DIGIT(c) ((unsigned)((c)-'0') <= 9u)

  // 10^0 .. 10^22 are exactly representable as double
  // JsonNumber/JsonFixed can be constructed directly (without fn()/fx()): always limit the precision
  inline constexpr uint8_t jsonClampPrecision(const uint8_t p){ return p > MAX_FLOAT_PRECISION ? uint8_t(MAX_FLOAT_PRECISION) : p; }
  inline constexpr double JSON_POW10[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
    1e20, 1e21, 1e22
  };
  constexpr uint8_t JSON_POW10_MAX_EXACT = 22;

  // Type names (for error messages only)
  template<typename T>
  constexpr const char* cppTypeName([[maybe_unused]] bool memberTypeName = false) {
      using U = unwrap_numeric_t<T>;
      using R = remove_cvref_t<U>;

      // optional -> name of the contained type
      if constexpr (is_json_optional_v<R>)
        return cppTypeName<typename json_optional_value<R>::type>(memberTypeName);

      //string pointers
      if constexpr (std::is_same_v<R, const char*> || std::is_same_v<R, char*>)
        return "string";

      //null / pointer
      if constexpr (std::is_same_v<R, std::nullptr_t>)
        return "null";
      else if constexpr (std::is_pointer_v<R>)
        return cppTypeName<std::remove_pointer_t<R>>(memberTypeName);

      //arrays
      if constexpr (std::is_array_v<R>) {
        if(memberTypeName) return "array";
        return cppTypeName<std::remove_extent_t<R>>(memberTypeName);
      }

      //floating
      if constexpr (std::is_same_v<R, float>)  return "float";
      if constexpr (std::is_same_v<R, double>) return "double";

      //bool
      if constexpr (std::is_same_v<R, bool>) return "bool";

      if constexpr (std::is_same_v<R, char>) return "char";
      if constexpr (std::is_same_v<R, signed char>) return "int8_t";
      if constexpr (std::is_same_v<R, unsigned char>) return "uint8_t";

      //ints
      if constexpr (std::is_integral_v<R>){
        if constexpr (std::is_signed_v<R>){
          if constexpr (sizeof(R) == 1) return "int8_t";
          if constexpr (sizeof(R) == 2) return "int16_t";
          if constexpr (sizeof(R) == 4) return "int32_t";
          if constexpr (sizeof(R) == 8) return "int64_t";
        }else{
          if constexpr (sizeof(R) == 1) return "uint8_t";
          if constexpr (sizeof(R) == 2) return "uint16_t";
          if constexpr (sizeof(R) == 4) return "uint32_t";
          if constexpr (sizeof(R) == 8) return "uint64_t";
        }
      }

      //enums
      if constexpr (std::is_enum_v<R>) return "enum";

      //objects
      if constexpr (is_reflectable_v<R>) return "struct";

      return "unknown";
  }
ENDREGION()

REGION(numformat)
  // ---------------------------------------------------------------------------------------------
  // Shared number formatting for JSONwriter and INIwriter (identical output in both libraries).
  // All functions write the characters to out WITHOUT a terminating '\0' and return the count.
  // out must have room for REFLECT_NUM_BUF_SIZE bytes.
  // ---------------------------------------------------------------------------------------------
  constexpr size_t REFLECT_NUM_BUF_SIZE = 32;

  // Digit pairs "00".."99" for fast integer output (two digits per division)
  inline constexpr char JSON_DIGIT_PAIRS[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

  // Write two digits ("00".."99") with byte access. memcpy would be a real function call on small MCUs.
  FORCE_INLINE void reflectPutPair(char *p, const uint32_t v){
    const char *const d = JSON_DIGIT_PAIRS + v * 2;
    p[0] = d[0];
    p[1] = d[1];
  }

  // Number of decimal digits of x (1..10). Branching search instead of 9 comparisons or a division:
  // much cheaper on 32-bit MCUs without division/FPU.
  FORCE_INLINE unsigned reflectDigitCount32(const uint32_t x){
    return (x < 10000u)
      ? ((x < 100u) ? 1u + (x >= 10u) : 3u + (x >= 1000u))
      : ((x < 1000000u) ? 5u + (x >= 100000u)
                        : ((x < 100000000u) ? 7u + (x >= 10000000u) : 9u + (x >= 1000000000u)));
  }

  // Writes the digits of x backwards so that the last digit is at p[-1]; returns the first digit.
  // One division per two digits (same loop as the original JSONwriter::appendUnsigned32).
  FORCE_INLINE char *reflectWriteDigitsBackward(char *p, uint32_t x){
    while(x >= 100){
      const uint32_t q = x / 100;
      const uint32_t r = x - ((q << 6) + (q << 5) + (q << 2));   // x - q*100, without the compiler turning it into a second division
      p -= 2;
      reflectPutPair(p, r);
      x = q;
    }
    if(x >= 10){
      p -= 2;
      reflectPutPair(p, x);
    }else{
      *--p = char('0' + x);
    }
    return p;
  }

  // Exactly n digits of x (x < 10^n) with leading zeros, e.g. fraction digits. n == 0 writes nothing.
  FORCE_INLINE void reflectWriteDigits32(char *out, const uint32_t x, const unsigned n){
    if(n == 0) return;
    char *p = reflectWriteDigitsBackward(out + n, x);
    while(p > out) *--p = '0';
  }

  FORCE_INLINE size_t reflectFormatU32(const uint32_t x, char *out){
    const unsigned n = reflectDigitCount32(x);
    reflectWriteDigitsBackward(out + n, x);
    return n;
  }

  inline size_t reflectFormatU64(uint64_t x, char *out){
    if(x <= 0xFFFFFFFFull) return reflectFormatU32((uint32_t)x, out);

    // from here on at least 10 digits
    unsigned n = 10;
    uint64_t lim = 10000000000ull;
    while(n < 20 && x >= lim){
      n++;
      lim *= 10;
    }
    char *p = out + n;
    // 64-bit division is slow on 32-bit MCUs: switch to 32 bit as early as possible
    while(x > 0xFFFFFFFFull){
      const uint64_t q = x / 100;
      const uint32_t r = (uint32_t)(x - q * 100);
      p -= 2;
      reflectPutPair(p, r);
      x = q;
    }
    reflectWriteDigitsBackward(p, (uint32_t)x);
    return n;
  }

  template<typename I>
  inline size_t reflectFormatInt(const I v, char *out){
    static_assert(std::is_integral_v<I>, "reflectFormatInt: integral type expected");
    size_t s = 0;
    if constexpr (sizeof(I) <= sizeof(uint32_t)){
      uint32_t mag;
      if constexpr (std::is_signed_v<I>){
        if(v < 0){ out[s++] = '-'; mag = uint32_t(0) - (uint32_t)(int32_t)v; }
        else mag = (uint32_t)v;
      }else{
        mag = (uint32_t)v;
      }
      return s + reflectFormatU32(mag, out + s);
    }else{
      uint64_t mag;
      if constexpr (std::is_signed_v<I>){
        if(v < 0){ out[s++] = '-'; mag = uint64_t(0) - (uint64_t)v; }
        else mag = (uint64_t)v;
      }else{
        mag = (uint64_t)v;
      }
      return s + reflectFormatU64(mag, out + s);
    }
  }

  // Integer part + '.' + exactly `decimals` fraction digits (frac < 10^decimals), with '-' if neg.
  inline size_t reflectFormatFixed(const bool neg, const uint32_t ip, const uint32_t frac, const unsigned decimals, char *out){
    size_t n = 0;
    if(neg && (ip != 0 || frac != 0)) out[n++] = '-';     // no "-0.00"
    n += reflectFormatU32(ip, out + n);
    if(decimals){
      out[n++] = '.';
      reflectWriteDigits32(out + n, frac, decimals);       // leading zeros included
      n += decimals;
    }
    return n;
  }

  // Same as reflectFormatFixed() with a 64-bit integer part (|value| >= 4e9).
  inline size_t reflectFormatFixed64(const bool neg, const uint64_t ip, const uint32_t frac, const unsigned decimals, char *out){
    size_t n = 0;
    if(neg) out[n++] = '-';
    n += reflectFormatU64(ip, out + n);
    if(decimals){
      out[n++] = '.';
      reflectWriteDigits32(out + n, frac, decimals);
      n += decimals;
    }
    return n;
  }

  // |value| >= 2^64: more than 20 integer digits do not fit, exponential notation (valid JSON, readable by INIreader).
  // digits: 9 for float, 17 for double (enough for an exact roundtrip).
  REFLECT_NOINLINE inline size_t reflectFormatExp(const double absValue, const bool neg, const int digits, char *out){
    const int n = snprintf(out, REFLECT_NUM_BUF_SIZE, "%s%.*g", neg ? "-" : "", digits, absValue);
    return (n > 0 && n < (int)REFLECT_NUM_BUF_SIZE) ? size_t(n) : size_t(0);   // 0 = error (cannot happen for finite values)
  }

  // Rare paths (|value| >= 4e9), deliberately not inline (keeps the hot path small: registers, prologue). Exact like the normal path: the integer part is
  // computed in 64 bit from mantissa and exponent, the fraction with integer arithmetic (no value * 10^d in
  // floating point, which would destroy the last digits).
  REFLECT_NOINLINE inline size_t reflectFormatLargeFloat(const uint32_t absBits, const bool neg, const unsigned decimals, char *out){
    const int32_t ex = int32_t(absBits >> 23);
    if(ex >= 127 + 64){                                   // >= 2^64
      float a;
      memcpy(&a, &absBits, sizeof(a));
      return reflectFormatExp((double)a, neg, 9, out);
    }
    // a float >= 4e9 (> 2^24) is always an integer: value = m * 2^(ex - 150), ex - 150 in 8..40
    const uint64_t ip = (uint64_t)((absBits & 0x7FFFFFu) | 0x800000u) << (ex - 150);
    return reflectFormatFixed64(neg, ip, 0, decimals, out);
  }
  REFLECT_NOINLINE inline size_t reflectFormatLargeDouble(const uint64_t absBits, const bool neg, const unsigned decimals, const uint32_t p10, char *out){
    const int32_t ex = int32_t(absBits >> 52);
    if(ex >= 1023 + 64){                                  // >= 2^64
      double a;
      memcpy(&a, &absBits, sizeof(a));
      return reflectFormatExp(a, neg, 17, out);
    }
    const uint64_t m = (absBits & 0xFFFFFFFFFFFFFull) | 0x10000000000000ull;   // value = m / 2^sh
    const int32_t sh = 1075 - ex;                         // >= 4e9 (> 2^31): sh <= 21
    uint64_t ip;
    uint32_t frac = 0;
    if(sh <= 0){
      ip = m << -sh;                                      // < 2^64 (shift <= 11)
    }else{
      ip = m >> sh;
      const uint64_t rem = m & ((uint64_t(1) << sh) - 1u);           // < 2^21
      frac = (uint32_t)((rem * p10 + (uint64_t(1) << (sh - 1))) >> sh);   // rem * 10^d < 2^48
      if(frac >= p10){ frac -= p10; ip++; }
    }
    return reflectFormatFixed64(neg, ip, frac, decimals, out);
  }

  // Other floating point types (long double with more than 53 bits, e.g. x86; ReflectJSON only - not on
  // the supported MCUs): floating point arithmetic as before the shared formatter.
  template<typename F>
  REFLECT_NOINLINE size_t reflectFormatRealGeneric(F value, const unsigned decimals, const uint32_t p10, char *out){
    const bool neg = value < 0;
    if(neg) value = -value;
    if(value < (F)4.0e9){
      uint32_t ip = (uint32_t)value;
      const F frac = value - (F)ip;
      uint32_t fp = (uint32_t)(frac * (F)p10 + (F)0.5);
      if(fp >= p10){ fp -= p10; ip++; }
      return reflectFormatFixed(neg, ip, fp, decimals, out);
    }
    const F scaled = value * (F)p10 + (F)0.5;
    if(scaled < (F)1.8e19){
      const uint64_t total = (uint64_t)scaled;
      const uint64_t ip = decimals ? total / p10 : total;
      return reflectFormatFixed64(neg, ip, (uint32_t)(total - ip * p10), decimals, out);
    }
    return reflectFormatExp((double)value, neg, 17, out);
  }

  // Number with a fixed number of decimal places (value must be finite, decimals <= MAX_FLOAT_PRECISION).
  // |value| >= 2^64 (1.8e19) is written in exponential notation. Returns 0 only if that fails.
  //
  // float and double are decomposed into mantissa and exponent: integer part and the rounded fraction
  // are computed with integer arithmetic only (no float operation - fast on MCUs without FPU / without
  // double FPU) and EXACTLY from the binary value (round half up), e.g. 2.675 (= 2.67499999...) -> "2.67".
  template<typename F>
  inline size_t reflectFormatReal(F value, unsigned decimals, char *out){
    if(decimals > MAX_FLOAT_PRECISION) decimals = MAX_FLOAT_PRECISION;
    const uint32_t p10 = (uint32_t)JSON_POW10[decimals];

    if constexpr (std::is_same_v<F, float>){
      uint32_t bits;
      memcpy(&bits, &value, sizeof(bits));
      const bool neg = (bits >> 31) != 0;
      const uint32_t absBits = bits & 0x7FFFFFFFu;
      if(absBits >= 0x4F6E6B28u){                        // |value| >= 4.0e9f: rare path
        return reflectFormatLargeFloat(absBits, neg, decimals, out);
      }
      const int32_t ex = int32_t(absBits >> 23);
      if(ex == 0) return reflectFormatFixed(neg, 0, 0, decimals, out);  // zero / subnormal (< 1.2e-38)
      const uint32_t m = (absBits & 0x7FFFFFu) | 0x800000u;             // value = m * 2^(ex - 150)
      const int32_t sh = 150 - ex;                                       // value = m / 2^sh
      uint32_t ip, frac;
      if(sh <= 0){                                                       // integer (2^23 .. 4e9): no fraction
        ip = m << -sh;
        frac = 0;
      }else{
        ip = sh < 24 ? (m >> sh) : 0u;
        const uint32_t rem = sh < 24 ? (m & ((1u << sh) - 1u)) : m;       // fraction = rem / 2^sh
        // fraction digits = round(rem * 10^d / 2^sh); rem < 2^24, 10^d < 2^27 -> product < 2^51
        if(sh >= 52){
          frac = 0;
        }else{
          const uint64_t prod = (uint64_t)rem * p10;
          frac = (uint32_t)((prod + (uint64_t(1) << (sh - 1))) >> sh);
        }
        if(frac >= p10){ frac -= p10; ip++; }                           // round up across the decimal point
      }
      return reflectFormatFixed(neg, ip, frac, decimals, out);
    }else if constexpr (std::is_same_v<F, double>){
      uint64_t bits;
      memcpy(&bits, &value, sizeof(bits));
      const bool neg = (bits >> 63) != 0;
      const uint64_t absBits = bits & 0x7FFFFFFFFFFFFFFFull;
      if(absBits >= 0x41EDCD6500000000ull){              // |value| >= 4.0e9: rare path
        return reflectFormatLargeDouble(absBits, neg, decimals, p10, out);
      }
      const int32_t ex = int32_t(absBits >> 52);
      if(ex == 0) return reflectFormatFixed(neg, 0, 0, decimals, out);  // zero / subnormal
      const uint64_t m = (absBits & 0xFFFFFFFFFFFFFull) | 0x10000000000000ull;   // value = m / 2^sh
      const int32_t sh = 1075 - ex;
      uint32_t ip, frac;
      if(sh <= 0){
        ip = (uint32_t)(m << -sh);
        frac = 0;
      }else{
        ip = sh < 53 ? (uint32_t)(m >> sh) : 0u;
        const uint64_t rem = sh < 53 ? (m & ((uint64_t(1) << sh) - 1u)) : m;   // < 2^53
        if(sh > 81){                                                       // rem * 10^d < 2^80 < 2^(sh-1)
          frac = 0;
        }else{
          // 96-bit product rem * 10^d = hi * 2^32 + lo32, then round and shift by sh (all integer)
          const uint64_t lo = (uint64_t)(uint32_t)rem * p10;               // < 2^59
          uint64_t hi = (rem >> 32) * p10 + (lo >> 32);                    // < 2^49
          const uint32_t lo32 = (uint32_t)lo;
          if(sh <= 32){
            // product = hi * 2^32 + lo32 < 2^(sh + 27) <= 2^59: fits into 64 bit
            const uint64_t prod = (hi << 32) | lo32;
            frac = (uint32_t)((prod + (uint64_t(1) << (sh - 1))) >> sh);
          }else{
            hi += uint64_t(1) << (sh - 33);                                // + 2^(sh-1) (rounding)
            frac = (uint32_t)(hi >> (sh - 32));
          }
        }
        if(frac >= p10){ frac -= p10; ip++; }
      }
      return reflectFormatFixed(neg, ip, frac, decimals, out);
    }else if constexpr (sizeof(F) == sizeof(double) && std::numeric_limits<F>::digits == 53){
      // long double that is a double (ESP32, RP2040, Teensy 4.x, ...): exact double path
      return reflectFormatReal((double)value, decimals, out);
    }else{
      return reflectFormatRealGeneric(value, decimals, p10, out);
    }
  }
ENDREGION()

#endif // REFLECTCORE_H
