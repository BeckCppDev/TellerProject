#pragma once

#include <Teller/Units/Literals.hpp>

#include <concepts>
#include <limits>

// Windows.hのマクロに書き換えられることを防ぐため、maxに括弧を付けること

namespace Teller
{
  // 1秒あたりのフレーム数
  // 単位のない整数はfpsとして扱う
  // timer()がフレーム数から秒を求めるので整数に限る
  class FrameRate
  {
  public:
    constexpr FrameRate(int fps) noexcept
      : fps_(fps)
    {
    }

    template <std::floating_point T>
    FrameRate(T) = delete;

    [[nodiscard]] constexpr int fps() const noexcept
    {
      return fps_;
    }

    [[nodiscard]] constexpr bool operator==(const FrameRate&) const noexcept = default;

  private:
    int fps_;
  };

  namespace literals
  {
    consteval FrameRate operator""_fps(unsigned long long value)
    {
      if (value == 0 || value > static_cast<unsigned long long>((std::numeric_limits<int>::max)()))
      {
        // constevalの中で到達するとコンパイルエラーになる
        throw "FrameRate out of range";
      }
      return FrameRate(static_cast<int>(value));
    }
  }
}
