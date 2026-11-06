#pragma once

namespace Teller
{
  // .smooth()で始めた変化がどこまで進んだか
  enum class Progress
  {
    // 変化を始めた瞬間
    started,
    // 変化している途中
    running,
    // 変化が終わった後
    finished,
  };

  // .smooth()の戻り値
  class SmoothState
  {
  public:
    constexpr SmoothState(Progress progress, double value) noexcept
      : progress_(progress)
      , value_(value)
    {
    }

    // 経過した時間の割合
    // startedは0.0、runningは0.0より大きく1.0未満、finishedは1.0
    [[nodiscard]] constexpr double value() const noexcept
    {
      return value_;
    }

    [[nodiscard]] constexpr bool operator==(Progress progress) const noexcept
    {
      return progress_ == progress;
    }

  private:
    Progress progress_;
    double value_;
  };
}
