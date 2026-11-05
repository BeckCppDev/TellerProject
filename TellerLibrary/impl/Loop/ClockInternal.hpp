#pragma once

// timer()の時計を進める側の入口

namespace Teller::Internal
{
  void setClockFps(int fps);
  void advanceClock();
}
