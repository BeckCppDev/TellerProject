#include <Teller/Loop/LoopController.hpp>

#include <Loop/ClockInternal.hpp>
#include <Smooth/Transition.hpp>
#include <Window/WindowInternal.hpp>

#include <SDL3/SDL.h>

#include <cstdint>

namespace Teller::Internal
{
  namespace
  {
    constexpr int defaultFps = 30;

    bool hasActiveController = false;
    int activeLogicFps = defaultFps;

    // 待ち時間の調整にだけ使う
    // ゲームの計算に使うとTASの再現性が崩れる
    struct PacingState
    {
      bool started = false;
      Uint64 baseNs = 0;
      std::uint64_t ticksSinceBase = 0;
    };
    PacingState pacing;

    int validFps(FrameRate rate)
    {
      if (rate.fps() > 0)
      {
        return rate.fps();
      }
#ifndef NDEBUG
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::LoopController: logic must be greater than 0, so %d fps is used", defaultFps);
#endif
      return defaultFps;
    }

    void waitUntil(Uint64 targetNs)
    {
      const Uint64 now = SDL_GetTicksNS();
      if (now < targetNs)
      {
        SDL_DelayPrecise(targetNs - now);
      }
    }
  }
}

namespace Teller
{
  LoopController::LoopController(const LoopControllerConfig& config)
    : config_(config)
  {
    if (Internal::hasActiveController)
    {
#ifndef NDEBUG
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::LoopController: only the first LoopController works");
#endif
      return;
    }
    Internal::hasActiveController = true;
    isActive_ = true;
    Internal::activeLogicFps = Internal::validFps(config_.logic);
    Internal::setClockFps(Internal::activeLogicFps);
    Internal::setClockActive(true);
    Internal::setEventsHandledByLoop(true);
  }

  LoopController::~LoopController()
  {
    if (!isActive_)
    {
      return;
    }
    Internal::hasActiveController = false;
    Internal::pacing = {};
    Internal::setClockActive(false);
    Internal::setEventsHandledByLoop(false);
  }

  void LoopController::update()
  {
    if (!isActive_)
    {
      return;
    }

    Internal::PacingState& pacing = Internal::pacing;
    const Uint64 now = SDL_GetTicksNS();

    if (!pacing.started)
    {
      pacing.started = true;
      pacing.baseNs = now;
      pacing.ticksSinceBase = 0;
    }

    // 周期の端数による誤差がたまらないよう、基準の時刻からn回目の時刻を求める
    const auto fps = static_cast<Uint64>(Internal::activeLogicFps);
    const Uint64 periodNs = SDL_NS_PER_SECOND / fps;
    Uint64 previousNs = pacing.baseNs + pacing.ticksSinceBase * SDL_NS_PER_SECOND / fps;
    ++pacing.ticksSinceBase;
    Uint64 targetNs = pacing.baseNs + pacing.ticksSinceBase * SDL_NS_PER_SECOND / fps;

    if (now >= targetNs && now - targetNs >= periodNs)
    {
      // 更新が詰まるのを防ぐため、今を基準にし直す
      pacing.baseNs = now;
      pacing.ticksSinceBase = 0;
      previousNs = now;
      targetNs = now;
    }

    // 頻度がlogicより高い変化は周期の途中でも進める
    Internal::advanceTransitions(Internal::clockFrames() + 1, [previousNs, targetNs](double position)
    {
      Internal::waitUntil(previousNs + static_cast<Uint64>(static_cast<double>(targetNs - previousNs) * position));
    });

    Internal::waitUntil(targetNs);
    Internal::advanceClock();
    Internal::processEvents();
  }
}
