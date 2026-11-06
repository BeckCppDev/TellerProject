#pragma once

#include <Teller/Smooth/Easing.hpp>
#include <Teller/Smooth/Progress.hpp>
#include <Teller/Units/Duration.hpp>

#include <cstdint>
#include <functional>
#include <optional>

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
      std::function<int()> fps;
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

    // 次に進める位置がframeの周期の中に来ていれば、その位置を返す
    // 位置は周期の始めが0.0、終わりが1.0
    [[nodiscard]] std::optional<double> nextPosition(std::uint64_t frame) const;

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
    // 始めたときの頻度
    std::int64_t fps_ = 0;
    std::uint64_t startFrame_ = 0;
  };

  // frameの周期の中で進める変化を位置の順に進める
  // 進める前に、その位置をwaitUntilに渡して呼ぶ
  void advanceTransitions(std::uint64_t frame, const std::function<void(double)>& waitUntil);
}
