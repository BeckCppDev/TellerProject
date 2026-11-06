#include <Teller/Smooth/Change.hpp>

#include <Smooth/Transition.hpp>

#include <SDL3/SDL.h>

namespace Teller
{
  Change::Change(Internal::Transition& transition, double target) noexcept
    : transition_(&transition)
    , target_(target)
  {
  }

  Change::~Change()
  {
    if (!smoothed_)
    {
      transition_->setNow(target_);
    }
  }

  SmoothState Change::smooth(Duration time, Easing easing)
  {
    smoothed_ = true;
    return transition_->smooth(target_, time, easing);
  }

  SmoothState Change::smooth(Duration time, int easingId)
  {
    // Easingに種類を足したら上限も変える
    constexpr int lastId = static_cast<int>(Easing::easeOutBack);
    if (easingId < 0 || easingId > lastId)
    {
#ifndef NDEBUG
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Change: easing %d does not exist, so linear is used", easingId);
#endif
      easingId = static_cast<int>(Easing::linear);
    }
    return smooth(time, static_cast<Easing>(easingId));
  }
}
