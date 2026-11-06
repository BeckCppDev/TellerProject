#pragma once

#include <Teller/Smooth/Easing.hpp>
#include <Teller/Smooth/Progress.hpp>
#include <Teller/Units/Duration.hpp>

namespace Teller
{
  namespace Internal
  {
    class Transition;
  }

  // セッターが返す変更の指示
  // smooth()を付けなければ、その文の終わりで即座に反映する
  class Change
  {
  public:
    // ライブラリの内部で使う
    Change(Internal::Transition& transition, double target) noexcept;

    ~Change();

    Change(const Change&) = delete;
    Change& operator=(const Change&) = delete;
    Change(Change&&) = delete;
    Change& operator=(Change&&) = delete;

    // 毎ループ同じ目標で呼んだときは変化を続ける
    // 途中で目標が変わったときや、変化の後に値がずれたときは、今の値から変化をやり直す
    // LoopControllerがないときや時間が0のときは即座に反映する
    SmoothState smooth(Duration time, Easing easing = Easing::linear);

    // 存在しない番号はlinearとして扱う
    SmoothState smooth(Duration time, int easingId);

  private:
    Internal::Transition* transition_;
    double target_;
    bool smoothed_ = false;
  };
}
