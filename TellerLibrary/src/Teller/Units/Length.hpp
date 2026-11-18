#pragma once

#include <Teller/Units/Literals.hpp>

#include <concepts>
#include <limits>

// Windows.hのマクロに書き換えられることを防ぐため、maxに括弧を付けること

namespace Teller
{
  // 画面上の長さ
  // 単位のない数値はpxとして扱い、小数も受け付ける
  // ウインドウの幅や縁の太さなど小数が意味を持たない所では、0の方向へ切り捨てる
  class Length
  {
  public:
    // trueを1pxとして受け付けない
    template <typename T>
      requires(std::integral<T> || std::floating_point<T>) && (!std::same_as<T, bool>)
    constexpr Length(T px) noexcept
      : px_(static_cast<double>(px))
    {
    }

    [[nodiscard]] constexpr double px() const noexcept
    {
      return px_;
    }

    [[nodiscard]] constexpr Length operator-() const noexcept
    {
      return Length(-px_);
    }

    [[nodiscard]] constexpr bool operator==(const Length&) const noexcept = default;

  private:
    double px_;
  };

  namespace Internal
  {
    // 小数が意味を持たない所で使う整数のpx
    // 0の方向へ切り捨て、intに収まらない値は端に寄せ、NaNは0にする
    [[nodiscard]] constexpr int truncatedPx(Length length) noexcept
    {
      const double px = length.px();
      // NaNは自分自身と等しくない
      if (px != px)
      {
        return 0;
      }
      if (px >= static_cast<double>((std::numeric_limits<int>::max)()))
      {
        return (std::numeric_limits<int>::max)();
      }
      if (px <= static_cast<double>((std::numeric_limits<int>::min)()))
      {
        return (std::numeric_limits<int>::min)();
      }
      return static_cast<int>(px);
    }
  }

  namespace literals
  {
    consteval Length operator""_px(unsigned long long value)
    {
      if (value > static_cast<unsigned long long>((std::numeric_limits<int>::max)()))
      {
        // constevalの中で到達するとコンパイルエラーになる
        throw "Length out of range";
      }
      return Length(static_cast<int>(value));
    }

    // 小数のリテラル
    consteval Length operator""_px(long double value)
    {
      if (value > static_cast<long double>((std::numeric_limits<int>::max)()))
      {
        throw "Length out of range";
      }
      return Length(static_cast<double>(value));
    }
  }
}
