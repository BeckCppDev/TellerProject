#include <Smooth/SmoothRate.hpp>

#include <Loop/ClockInternal.hpp>

#include <SDL3/SDL.h>

namespace Teller::Internal
{
  SmoothRateSetting::SmoothRateSetting(const char* owner) noexcept
    : owner_(owner)
  {
  }

  void SmoothRateSetting::set(std::optional<FrameRate> rate)
  {
    if (rate && rate->fps() <= 0)
    {
#ifndef NDEBUG
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: smoothRate must be greater than 0, so logic is used", owner_);
#endif
      rate.reset();
    }
    rate_ = rate;
  }

  void SmoothRateSetting::reset() noexcept
  {
    rate_.reset();
  }

  int SmoothRateSetting::fps() const
  {
    return rate_ ? rate_->fps() : clockFps();
  }
}
