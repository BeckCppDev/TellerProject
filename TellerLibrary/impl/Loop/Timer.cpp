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
    };
    ClockState clockState;
  }

  void setClockFps(int fps)
  {
    clockState.logicFps = fps;
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
    const Internal::ClockState& clockState = Internal::clockState;
    ElapsedTime result;
    result.frames = clockState.frames;
    result.ms = clockState.frames * 1000 / static_cast<std::uint64_t>(clockState.logicFps);
    result.sec = static_cast<double>(clockState.frames) / clockState.logicFps;
    result.min = result.sec / 60.0;
    return result;
  }
}
