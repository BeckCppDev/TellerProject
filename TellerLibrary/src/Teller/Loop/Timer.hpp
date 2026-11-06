#pragma once

#include <cstdint>

namespace Teller
{
  // timer()の戻り値
  class ElapsedTime
  {
  public:
    // fpsは1以上を渡す
    constexpr ElapsedTime(std::uint64_t frames, int fps) noexcept
      : frames_(frames)
      , fps_(static_cast<std::uint64_t>(fps))
    {
    }

    [[nodiscard]] constexpr std::uint64_t frames() const noexcept
    {
      return frames_;
    }

    // 小数点以下は切り捨てる
    [[nodiscard]] constexpr std::uint64_t ms() const noexcept
    {
      return frames_ * 1000 / fps_;
    }

    [[nodiscard]] constexpr double sec() const noexcept
    {
      return static_cast<double>(frames_) / static_cast<double>(fps_);
    }

    // Windows.hのminマクロに置き換えられないよう、名前を括弧で囲む
    [[nodiscard]] constexpr double(min)() const noexcept
    {
      return sec() / 60.0;
    }

  private:
    std::uint64_t frames_;
    std::uint64_t fps_;
  };

  // 起動してからの経過時間
  // TASの再現性のため、PCの時計ではなくlogicの更新回数から求める
  // LoopController::update()が進めるので、それがないときは0を返す
  [[nodiscard]] ElapsedTime timer();
}
