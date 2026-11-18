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

    // trueを1fpsとして受け付けない
    template <typename T>
      requires std::floating_point<T> || std::same_as<T, bool>
    FrameRate(T) = delete;

    [[nodiscard]] constexpr int fps() const noexcept
    {
      return fps_;
    }

    // 1フレームの長さ
    // 小数点以下は切り捨てる
    // 0以下のfpsでは0を返す
    [[nodiscard]] constexpr int ms() const noexcept
    {
      return fps_ > 0 ? 1000 / fps_ : 0;
    }

    // 1フレームの長さ
    // 0以下のfpsでは0を返す
    [[nodiscard]] constexpr double sec() const noexcept
    {
      return fps_ > 0 ? 1.0 / static_cast<double>(fps_) : 0.0;
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
