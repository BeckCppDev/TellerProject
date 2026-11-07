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
    bool isFullscreen = false;
    bool videoInitialized = false;

    Transition widthTransition;
    Transition heightTransition;
    Transition xTransition;
    Transition yTransition;
  };

  void processEvents();

  // falseのときはWindow::isOpen()と同梱のmain()がprocessEvents()を呼ぶ
  void setEventsHandledByLoop(bool handled);
  [[nodiscard]] bool areEventsHandledByLoop();
}
