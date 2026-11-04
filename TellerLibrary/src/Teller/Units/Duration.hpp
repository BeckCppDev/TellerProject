#pragma once

#include <concepts>
#include <cstdint>
#include <limits>

namespace Teller
{
  // 時間の長さ
  // 単位のない数値は秒として扱う
  class Duration
  {
  public:
    // trueを1秒として受け付けない
    template <typename T>
      requires (std::integral<T> || std::floating_point<T>) && (!std::same_as<T, bool>)
    constexpr Duration(T seconds) noexcept
      : microseconds_(0)
    {
      if constexpr (std::floating_point<T>)
      {
        // std::llroundはC++20ではconstexprでないため自前で四捨五入する
        const double scaled = static_cast<double>(seconds) * 1'000'000.0;
        microseconds_ = static_cast<std::int64_t>(scaled < 0.0 ? scaled - 0.5 : scaled + 0.5);
      }
      else
      {
        microseconds_ = static_cast<std::int64_t>(seconds) * 1'000'000;
      }
    }

    [[nodiscard]] static constexpr Duration fromMicroseconds(std::int64_t microseconds) noexcept
    {
      return Duration(MicrosecondsTag{}, microseconds);
    }

    [[nodiscard]] constexpr std::int64_t microseconds() const noexcept
    {
      return microseconds_;
    }

    [[nodiscard]] constexpr double seconds() const noexcept
    {
      return static_cast<double>(microseconds_) / 1'000'000.0;
    }

    [[nodiscard]] constexpr bool operator==(const Duration&) const noexcept = default;

  private:
    // 秒を受け取るコンストラクタと区別する
    struct MicrosecondsTag
    {
    };

    constexpr Duration(MicrosecondsTag, std::int64_t microseconds) noexcept
      : microseconds_(microseconds)
    {
    }

    std::int64_t microseconds_;
  };

  namespace literals
  {
    consteval Duration operator""_s(unsigned long long value)
    {
      if (value > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max() / 1'000'000))
      {
        // constevalの中で到達するとコンパイルエラーになる
        throw "Duration out of range";
      }
      return Duration::fromMicroseconds(static_cast<std::int64_t>(value) * 1'000'000);
    }

    consteval Duration operator""_s(long double value)
    {
      if (value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) / 1'000'000.0L)
      {
        throw "Duration out of range";
      }
      return Duration(static_cast<double>(value));
    }

    consteval Duration operator""_ms(unsigned long long value)
    {
      if (value > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max() / 1'000))
      {
        throw "Duration out of range";
      }
      return Duration::fromMicroseconds(static_cast<std::int64_t>(value) * 1'000);
    }

    consteval Duration operator""_ms(long double value)
    {
      if (value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) / 1'000.0L)
      {
        throw "Duration out of range";
      }
      const long double scaled = value * 1'000.0L;
      return Duration::fromMicroseconds(static_cast<std::int64_t>(scaled + 0.5L));
    }
  }
}
