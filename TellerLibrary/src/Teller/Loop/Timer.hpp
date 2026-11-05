#pragma once

#include <cstdint>

namespace Teller
{
  struct ElapsedTime
  {
    std::uint64_t frames = 0;

    // 小数点以下は切り捨てる
    std::uint64_t ms = 0;

    double sec = 0.0;
    double min = 0.0;
  };

  // 起動してからの経過時間
  // TASの再現性のため、PCの時計ではなくlogicの更新回数から求める
  // LoopController::update()が進めるので、それがないときは0を返す
  [[nodiscard]] ElapsedTime timer();
}
