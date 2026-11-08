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
    // 補間の仕組みができるまでは使わず、logicの周期で描く
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

    // Asset::update()で確定した内容を描き、次の更新時刻まで待ち、timer()を1フレーム進め、OSからの通知を処理する
    // 待っている間に.smooth()の変化をそれぞれの頻度で進める
    // 1周期以上遅れたときは遅れを取り戻さない
    virtual void update();

  private:
    LoopControllerConfig config_;
    bool isActive_ = false;
  };
}
