#include <Teller/Loop/Timer.hpp>

#include <Loop/ClockInternal.hpp>

#include <cstdint>

namespace Teller::Internal
{
  namespace
  {
    struct ClockState
    {
      std::uint64_t frames = 0;
      int logicFps = 30;
      bool active = false;
    };
    ClockState clockState;
  }

  void setClockFps(int fps)
  {
    clockState.logicFps = fps;
  }

  int clockFps()
  {
    return clockState.logicFps;
  }

  void setClockActive(bool active)
  {
    clockState.active = active;
  }

  bool isClockActive()
  {
    return clockState.active;
  }

  void advanceClock()
  {
    ++clockState.frames;
  }
}

namespace Teller
{
  ElapsedTime timer()
  {
    return ElapsedTime(Internal::clockState.frames, Internal::clockState.logicFps);
  }
}
