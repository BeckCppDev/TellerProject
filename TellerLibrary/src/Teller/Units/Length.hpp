#pragma once

#include <concepts>
#include <limits>

namespace Teller
{
  // 画面上の長さ
  // 単位のない整数はpxとして扱う
  class Length
  {
  public:
    constexpr Length(int px) noexcept
      : px_(px)
    {
    }

    // 小数の暗黙の切り捨てを防ぐ
    template <std::floating_point T>
    Length(T) = delete;

    [[nodiscard]] constexpr int px() const noexcept
    {
      return px_;
    }

    [[nodiscard]] constexpr Length operator-() const noexcept
    {
      return Length(-px_);
    }

    [[nodiscard]] constexpr bool operator==(const Length&) const noexcept = default;

  private:
    int px_;
  };

  namespace literals
  {
    consteval Length operator""_px(unsigned long long value)
    {
      if (value > static_cast<unsigned long long>(std::numeric_limits<int>::max()))
      {
        // constevalの中で到達するとコンパイルエラーになる
        throw "Length out of range";
      }
      return Length(static_cast<int>(value));
    }
  }
}
