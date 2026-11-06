#include <Smooth/Transition.hpp>

#include <Loop/ClockInternal.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace Teller::Internal
{
  namespace
  {
    // グローバルなWindowの初期化から使われるため、関数内の静的変数にする
    std::vector<Transition*>& registry()
    {
      static std::vector<Transition*> transitions;
      return transitions;
    }

    // 時間の割合tを進んだ距離の割合に変える
    // 環境による計算結果の差でTASの再現性が崩れないよう、std::powは使用禁止
    double ease(Easing easing, double t)
    {
      switch (easing)
      {
        case Easing::easeOutCubic:
        {
          const double u = 1.0 - t;
          return 1.0 - u * u * u;
        }
        case Easing::easeInOutCubic:
        {
          if (t < 0.5)
          {
            return 4.0 * t * t * t;
          }
          const double u = -2.0 * t + 2.0;
          return 1.0 - u * u * u / 2.0;
        }
        case Easing::easeInCubic:
        {
          return t * t * t;
        }
        case Easing::easeOutBack:
        {
          constexpr double overshoot = 1.70158;
          const double u = t - 1.0;
          return 1.0 + (overshoot + 1.0) * u * u * u + overshoot * u * u;
        }
        case Easing::linear:
        default:
        {
          return t;
        }
      }
    }

    // 端数は四捨五入する
    std::int64_t toTicks(Duration time, int fps)
    {
      const std::int64_t microseconds = time.microseconds();
      if (microseconds <= 0)
      {
        return 0;
      }
      // 桁あふれを防ぐため秒の整数部分と端数に分ける
      const std::int64_t wholeSeconds = microseconds / 1'000'000;
      const std::int64_t restMicroseconds = microseconds % 1'000'000;
      return wholeSeconds * fps + (restMicroseconds * fps + 500'000) / 1'000'000;
    }
  }

  Transition::Transition(Handlers handlers)
    : handlers_(std::move(handlers))
  {
    registry().push_back(this);
  }

  Transition::~Transition()
  {
    std::vector<Transition*>& transitions = registry();
    transitions.erase(std::remove(transitions.begin(), transitions.end(), this), transitions.end());
  }

  void Transition::setNow(double target)
  {
    cancel();
    handlers_.apply(target);
  }

  SmoothState Transition::smooth(double target, Duration time, Easing easing)
  {
    const std::int64_t ticks = isClockActive() ? toTicks(time, clockFps()) : 0;
    if (ticks <= 0 || !handlers_.canSmooth())
    {
      setNow(target);
      return SmoothState(Progress::finished, 1.0);
    }

    // 同じ目標なら変化を続ける
    if (hasTarget_ && to_ == target)
    {
      if (running_)
      {
        if (elapsedTicks_ == 0)
        {
          return SmoothState(Progress::started, 0.0);
        }
        return SmoothState(Progress::running, static_cast<double>(elapsedTicks_) / static_cast<double>(totalTicks_));
      }
      if (handlers_.read() == target)
      {
        return SmoothState(Progress::finished, 1.0);
      }
      // 完了後に値がずれていたらやり直す
    }

    hasTarget_ = true;
    running_ = true;
    from_ = handlers_.read();
    to_ = target;
    easing_ = easing;
    totalTicks_ = ticks;
    elapsedTicks_ = 0;
    return SmoothState(Progress::started, 0.0);
  }

  void Transition::cancel()
  {
    hasTarget_ = false;
    running_ = false;
  }

  void Transition::advance()
  {
    if (!running_)
    {
      return;
    }

    ++elapsedTicks_;
    if (elapsedTicks_ >= totalTicks_)
    {
      // 計算の誤差を残さないよう最後は目標の値ちょうどにする
      running_ = false;
      handlers_.apply(to_);
      return;
    }

    const double t = static_cast<double>(elapsedTicks_) / static_cast<double>(totalTicks_);
    handlers_.apply(from_ + (to_ - from_) * ease(easing_, t));
  }

  void advanceTransitions()
  {
    for (Transition* transition : registry())
    {
      transition->advance();
    }
  }
}
