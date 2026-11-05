#pragma once

#include <Teller/Window/Window.hpp>

#include <SDL3/SDL.h>

namespace Teller::Internal
{
  struct WindowState
  {
    WindowConfig config;
    SDL_Window* handle = nullptr;
    FullscreenMode toggleMode = FullscreenMode::borderless;
    bool isFullscreen = false;
    bool videoInitialized = false;
  };

  void processEvents();

  // falseのときはWindow::isOpen()がprocessEvents()を呼ぶ
  void setEventsHandledByLoop(bool handled);
}
