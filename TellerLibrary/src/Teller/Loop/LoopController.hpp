#pragma once

#include <Teller/Units/FrameRate.hpp>

namespace Teller
{
  // 指定する項目は宣言の順番どおりに並べる
  struct LoopControllerConfig
  {
    // 計算の更新回数
    FrameRate logic = 30;

    // 描画の更新回数
    // 描画の仕組みができるまでは使わない
    FrameRate display = 30;
  };

  // ループの最後でupdate()を呼ぶ
  // 動くのは最初に作った1つだけ
  class LoopController
  {
  public:
    explicit LoopController(const LoopControllerConfig& config = {});
    virtual ~LoopController();

    LoopController(const LoopController&) = delete;
    LoopController& operator=(const LoopController&) = delete;

    // 次の更新時刻まで待ち、timer()と.smooth()の変化を1フレーム進め、OSからの通知を処理する
    // 1周期以上遅れたときは遅れを取り戻さない
    virtual void update();

  private:
    LoopControllerConfig config_;
    bool isActive_ = false;
  };
}
