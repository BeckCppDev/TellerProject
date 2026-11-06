#pragma once

#include <Teller/Smooth/Easing.hpp>
#include <Teller/Smooth/Progress.hpp>
#include <Teller/Units/Duration.hpp>

#include <cstdint>
#include <functional>

namespace Teller::Internal
{
  // .smooth()で変える数値1つ分の状態
  // 作られている間は一覧に登録され、advanceTransitions()で進む
  class Transition
  {
  public:
    struct Handlers
    {
      std::function<double()> read;
      std::function<void(double)> apply;
      // falseのときは時間をかけずに反映する
      std::function<bool()> canSmooth;
    };

    explicit Transition(Handlers handlers);
    ~Transition();

    // 一覧にアドレスを登録するのでコピーと移動を禁止する
    Transition(const Transition&) = delete;
    Transition& operator=(const Transition&) = delete;
    Transition(Transition&&) = delete;
    Transition& operator=(Transition&&) = delete;

    void setNow(double target);

    SmoothState smooth(double target, Duration time, Easing easing);

    // 値はその時点のまま残る
    void cancel();

    void advance();

  private:
    Handlers handlers_;

    // 直前の.smooth()の目標を覚えているか
    bool hasTarget_ = false;
    bool running_ = false;
    double from_ = 0.0;
    double to_ = 0.0;
    Easing easing_ = Easing::linear;
    std::int64_t totalTicks_ = 0;
    std::int64_t elapsedTicks_ = 0;
  };

  // LoopController::update()が周期ごとに呼ぶ
  void advanceTransitions();
}
