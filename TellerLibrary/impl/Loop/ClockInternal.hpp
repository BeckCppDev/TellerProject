#pragma once

// timer()の時計を進める側の入口

namespace Teller::Internal
{
  void setClockFps(int fps);
  [[nodiscard]] int clockFps();

  // LoopControllerが時計を進めている間はtrueにする
  void setClockActive(bool active);
  [[nodiscard]] bool isClockActive();

  void advanceClock();
}
