#include <Window/WindowInternal.hpp>

namespace Teller::Internal
{
  namespace
  {
    // SDL_WindowからWindowStateを引くためのプロパティ名
    constexpr const char* statePropertyName = "Teller.WindowState";

    void logError(const char* what)
    {
#ifndef NDEBUG
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Window: %s failed: %s", what, SDL_GetError());
#else
      static_cast<void>(what);
#endif
    }

    // SDL3の最小・最大サイズでは0が制限なしを表す
    int pxOrZero(const std::optional<Length>& value)
    {
      return value ? value->px() : 0;
    }

    struct OsWindowState
    {
      int x = 0;
      int y = 0;
      int width = 0;
      int height = 0;
    };

    // OSから来る値は全てここを通す
    // TASの再生時に記録した値へ差し替える箇所
    OsWindowState readOsWindowState(SDL_Window* handle)
    {
      OsWindowState result;
      SDL_GetWindowPosition(handle, &result.x, &result.y);
      SDL_GetWindowSize(handle, &result.width, &result.height);
      return result;
    }

    void applyFullscreen(WindowState& state, FullscreenMode mode)
    {
      if (mode == FullscreenMode::windowed)
      {
        if (!SDL_SetWindowFullscreen(state.handle, false))
        {
          logError("SDL_SetWindowFullscreen");
          return;
        }
        state.isFullscreen = false;
        return;
      }

      if (mode == FullscreenMode::borderless)
      {
        if (!SDL_SetWindowFullscreenMode(state.handle, nullptr))
        {
          logError("SDL_SetWindowFullscreenMode");
          return;
        }
      }
      else
      {
        SDL_DisplayMode displayMode;
        const SDL_DisplayID display = SDL_GetDisplayForWindow(state.handle);
        if (!SDL_GetClosestFullscreenDisplayMode(display, state.config.width.px(), state.config.height.px(), 0.0f, false, &displayMode))
        {
          logError("SDL_GetClosestFullscreenDisplayMode");
          return;
        }
        if (!SDL_SetWindowFullscreenMode(state.handle, &displayMode))
        {
          logError("SDL_SetWindowFullscreenMode");
          return;
        }
      }

      if (!SDL_SetWindowFullscreen(state.handle, true))
      {
        logError("SDL_SetWindowFullscreen");
        return;
      }
      state.toggleMode = mode;
      state.isFullscreen = true;
    }

    void closeWindow(WindowState& state)
    {
      SDL_DestroyWindow(state.handle);
      state.handle = nullptr;
      state.isFullscreen = false;
    }

    WindowState* findState(SDL_WindowID id)
    {
      SDL_Window* handle = SDL_GetWindowFromID(id);
      if (handle == nullptr)
      {
        return nullptr;
      }
      return static_cast<WindowState*>(SDL_GetPointerProperty(SDL_GetWindowProperties(handle), statePropertyName, nullptr));
    }
  }

  void processEvents()
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      switch (event.type)
      {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
          if (WindowState* state = findState(event.window.windowID))
          {
            closeWindow(*state);
          }
          break;
        }
        case SDL_EVENT_KEY_DOWN:
        {
          if (event.key.key != SDLK_F4 || event.key.repeat)
          {
            break;
          }
          WindowState* state = findState(event.key.windowID);
          if (state != nullptr && state->config.allowFullscreenToggle)
          {
            applyFullscreen(*state, state->isFullscreen ? FullscreenMode::windowed : state->toggleMode);
          }
          break;
        }
        default:
        {
          break;
        }
      }
    }
  }
}

namespace Teller
{
  Window::Window(const WindowConfig& config)
    : state_(std::make_unique<Internal::WindowState>())
  {
    state_->config = config;
    if (config.fullscreen != FullscreenMode::windowed)
    {
      state_->toggleMode = config.fullscreen;
    }
  }

  Window::~Window()
  {
    if (state_->handle != nullptr)
    {
      Internal::closeWindow(*state_);
    }
    if (state_->videoInitialized)
    {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
  }

  void Window::create()
  {
    if (state_->handle != nullptr)
    {
      return;
    }

    const WindowConfig& config = state_->config;

#ifndef NDEBUG
    if (config.centered && (config.x || config.y))
    {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Window: centered is true, so x and y are ignored");
    }
#endif

    // SDL3の中で参照カウントされるので、Windowごとに呼んでよい
    if (!state_->videoInitialized)
    {
      if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
      {
        Internal::logError("SDL_InitSubSystem");
        return;
      }
      state_->videoInitialized = true;
    }

    const auto position = [&config](const std::optional<Length>& value) -> Sint64
    {
      if (config.centered || !value)
      {
        return SDL_WINDOWPOS_CENTERED;
      }
      return value->px();
    };

    // 設定途中の姿を見せないよう、隠した状態で作って最後に表示する
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, config.title.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, config.width.px());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, config.height.px());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, position(config.x));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, position(config.y));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, config.resizable);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, !config.decorated);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, config.alwaysOnTop);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    state_->handle = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    if (state_->handle == nullptr)
    {
      Internal::logError("SDL_CreateWindowWithProperties");
      return;
    }

    SDL_SetPointerProperty(SDL_GetWindowProperties(state_->handle), Internal::statePropertyName, state_.get());

    if (config.minWidth || config.minHeight)
    {
      SDL_SetWindowMinimumSize(state_->handle, Internal::pxOrZero(config.minWidth), Internal::pxOrZero(config.minHeight));
    }
    if (config.maxWidth || config.maxHeight)
    {
      SDL_SetWindowMaximumSize(state_->handle, Internal::pxOrZero(config.maxWidth), Internal::pxOrZero(config.maxHeight));
    }
    if (config.aspectRatio > 0.0f)
    {
      SDL_SetWindowAspectRatio(state_->handle, config.aspectRatio, config.aspectRatio);
    }
    if (config.fullscreen != FullscreenMode::windowed)
    {
      Internal::applyFullscreen(*state_, config.fullscreen);
    }

    SDL_ShowWindow(state_->handle);
  }

  bool Window::isOpen() const
  {
    return state_->handle != nullptr;
  }

  std::string Window::getTitle() const
  {
    return state_->config.title;
  }

  void Window::setTitle(std::string_view title)
  {
    state_->config.title = title;
    if (state_->handle != nullptr)
    {
      SDL_SetWindowTitle(state_->handle, state_->config.title.c_str());
    }
  }

  int Window::getWidth() const
  {
    if (state_->handle == nullptr)
    {
      return state_->config.width.px();
    }
    return Internal::readOsWindowState(state_->handle).width;
  }

  int Window::getHeight() const
  {
    if (state_->handle == nullptr)
    {
      return state_->config.height.px();
    }
    return Internal::readOsWindowState(state_->handle).height;
  }

  void Window::setWidth(Length width)
  {
    state_->config.width = width;
    if (state_->handle != nullptr)
    {
      // もう一方の軸はユーザーが変えている可能性があるので実際の値を使う
      SDL_SetWindowSize(state_->handle, width.px(), Internal::readOsWindowState(state_->handle).height);
    }
  }

  void Window::setHeight(Length height)
  {
    state_->config.height = height;
    if (state_->handle != nullptr)
    {
      SDL_SetWindowSize(state_->handle, Internal::readOsWindowState(state_->handle).width, height.px());
    }
  }

  int Window::getX() const
  {
    if (state_->handle == nullptr)
    {
      return Internal::pxOrZero(state_->config.x);
    }
    return Internal::readOsWindowState(state_->handle).x;
  }

  int Window::getY() const
  {
    if (state_->handle == nullptr)
    {
      return Internal::pxOrZero(state_->config.y);
    }
    return Internal::readOsWindowState(state_->handle).y;
  }

  void Window::setX(Length x)
  {
    state_->config.x = x;
    state_->config.centered = false;
    if (state_->handle != nullptr)
    {
      SDL_SetWindowPosition(state_->handle, x.px(), Internal::readOsWindowState(state_->handle).y);
    }
  }

  void Window::setY(Length y)
  {
    state_->config.y = y;
    state_->config.centered = false;
    if (state_->handle != nullptr)
    {
      SDL_SetWindowPosition(state_->handle, Internal::readOsWindowState(state_->handle).x, y.px());
    }
  }
}
