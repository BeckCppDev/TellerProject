#pragma once

#include <Teller/Window/Window.hpp>

#include <Smooth/Transition.hpp>

#include <SDL3/SDL.h>

namespace Teller::Internal
{
  struct WindowState
  {
    WindowState();

    WindowConfig config;
    SDL_Window* handle = nullptr;
    FullscreenMode toggleMode = FullscreenMode::borderless;
    bool isFullscreen = false;
    bool videoInitialized = false;

    Transition widthTransition;
    Transition heightTransition;
    Transition xTransition;
    Transition yTransition;
  };

  void processEvents();

  // falseのときはWindow::isOpen()がprocessEvents()を呼ぶ
  void setEventsHandledByLoop(bool handled);
}
