#include <Window/WindowInternal.hpp>
#include <Loop/ClockInternal.hpp>
#include <Teller/Asset/Asset.hpp>
#include <Teller/Loop/Quit.hpp>

#include <algorithm>
#include <cmath>

namespace Teller::Internal
{
  namespace
  {
    // SDL_WindowからWindowStateを引くためのプロパティ名
    constexpr const char* statePropertyName = "Teller.WindowState";

    bool eventsHandledByLoop = false;

    // 配布後の原因調査のためReleaseでも出す
    void logError(const char* what)
    {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Window: %s failed: %s", what, SDL_GetError());
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

      if (!SDL_SetWindowFullscreenMode(state.handle, nullptr))
      {
        logError("SDL_SetWindowFullscreenMode");
        return;
      }

      if (!SDL_SetWindowFullscreen(state.handle, true))
      {
        logError("SDL_SetWindowFullscreen");
        return;
      }
      state.isFullscreen = true;
    }

    void closeWindow(WindowState& state)
    {
      state.widthTransition.cancel();
      state.heightTransition.cancel();
      state.xTransition.cancel();
      state.yTransition.cancel();

      removeDrawListener(state);

      // GPU上の画像はレンダラーの破棄で消えるので、一覧だけを空にする
      state.textures.clear();
      if (state.renderer != nullptr)
      {
        SDL_DestroyRenderer(state.renderer);
        state.renderer = nullptr;
      }

      SDL_DestroyWindow(state.handle);
      state.handle = nullptr;
      state.isFullscreen = false;
    }

    void applyWidth(WindowState& state, int px)
    {
      state.config.width = px;
      if (state.handle != nullptr)
      {
        // もう一方の軸はユーザーが変えている可能性があるので実際の値を使う
        SDL_SetWindowSize(state.handle, px, readOsWindowState(state.handle).height);
      }
    }

    void applyHeight(WindowState& state, int px)
    {
      state.config.height = px;
      if (state.handle != nullptr)
      {
        SDL_SetWindowSize(state.handle, readOsWindowState(state.handle).width, px);
      }
    }

    void applyX(WindowState& state, int px)
    {
      state.config.x = px;
      state.config.centered = false;
      if (state.handle != nullptr)
      {
        SDL_SetWindowPosition(state.handle, px, readOsWindowState(state.handle).y);
      }
    }

    void applyY(WindowState& state, int px)
    {
      state.config.y = px;
      state.config.centered = false;
      if (state.handle != nullptr)
      {
        SDL_SetWindowPosition(state.handle, readOsWindowState(state.handle).x, px);
      }
    }

    int effectiveSmoothFps(const WindowState& state)
    {
      return state.config.smoothRate ? state.config.smoothRate->fps() : clockFps();
    }

    // 0以下の頻度は指定がなかったものとして扱う
    std::optional<FrameRate> validSmoothRate(std::optional<FrameRate> rate)
    {
      if (!rate || rate->fps() > 0)
      {
        return rate;
      }
#ifndef NDEBUG
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Window: smoothRate must be greater than 0, so logic is used");
#endif
      return std::nullopt;
    }

    Transition::Handlers makeHandlers(WindowState& state, int OsWindowState::*axis, void (*apply)(WindowState&, int))
    {
      return {
        .read = [&state, axis]
        {
          return static_cast<double>(readOsWindowState(state.handle).*axis);
        },
        .apply = [&state, apply](double value)
        {
          apply(state, static_cast<int>(std::lround(value)));
        },
        .canSmooth = [&state]
        {
          return state.handle != nullptr;
        },
        .fps = [&state]
        {
          return effectiveSmoothFps(state);
        },
      };
    }

    // create()の前に読み込んだ画像にも対応するため、最初に描くときに作る
    SDL_Texture* findTexture(WindowState& state, const ImageData& image)
    {
      if (const auto found = state.textures.find(&image); found != state.textures.end())
      {
        return found->second;
      }

      SDL_Texture* texture = SDL_CreateTextureFromSurface(state.renderer, image.surface);
      if (texture == nullptr)
      {
        logError("SDL_CreateTextureFromSurface");
        return nullptr;
      }

      // 既定のままだと拡大したときにぼやける
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
      state.textures.emplace(&image, texture);
      return texture;
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

  WindowState::WindowState()
    : widthTransition(makeHandlers(*this, &OsWindowState::width, applyWidth))
    , heightTransition(makeHandlers(*this, &OsWindowState::height, applyHeight))
    , xTransition(makeHandlers(*this, &OsWindowState::x, applyX))
    , yTransition(makeHandlers(*this, &OsWindowState::y, applyY))
  {
  }

  void WindowState::onFrame(std::span<const DrawMessage> messages)
  {
    if (renderer == nullptr)
    {
      return;
    }

    // 黒い帯の部分も含めて塗る
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    // expandは見える範囲が変わるので、今の大きさを使う
    int screenWidth = gameWidth;
    int screenHeight = gameHeight;
    if (config.resizeMode == Resize::expand)
    {
      SDL_GetCurrentRenderOutputSize(renderer, &screenWidth, &screenHeight);
    }

    const Color& background = config.backgroundColor;
    SDL_SetRenderDrawColor(renderer, background.r(), background.g(), background.b(), SDL_ALPHA_OPAQUE);
    const SDL_FRect screen{ 0.0f, 0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight) };
    SDL_RenderFillRect(renderer, &screen);

    // 利用者の座標は中央が原点でYが上向き、SDL3は左上が原点でYが下向き

    for (const DrawMessage& message : messages)
    {
      if (std::find(subscriptions.begin(), subscriptions.end(), message.image->source) == subscriptions.end())
      {
        continue;
      }

      SDL_Texture* texture = findTexture(*this, *message.image);
      if (texture == nullptr)
      {
        continue;
      }

      const auto width = static_cast<float>(message.image->surface->w);
      const auto height = static_cast<float>(message.image->surface->h);
      const SDL_FRect destination
      {
        static_cast<float>(screenWidth / 2.0 + message.x) - width / 2.0f,
        static_cast<float>(screenHeight / 2.0 - message.y) - height / 2.0f,
        width,
        height,
      };
      SDL_RenderTexture(renderer, texture, nullptr, &destination);
    }

    SDL_RenderPresent(renderer);
  }

  void WindowState::onImageReleased(const ImageData* image)
  {
    if (const auto found = textures.find(image); found != textures.end())
    {
      SDL_DestroyTexture(found->second);
      textures.erase(found);
    }
  }

  void WindowState::onSourceReleased(const void* source)
  {
    std::erase(subscriptions, source);
  }

  void processEvents()
  {
    // create()の前は受け取る通知がない
    if (SDL_WasInit(SDL_INIT_EVENTS) == 0)
    {
      return;
    }

#ifndef NDEBUG
    static bool warned = false;
    // 最初のupdate()の前にも来るのでLoopControllerの有無で判断する
    if (!isClockActive() && !warned)
    {
      warned = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Window: no LoopController, so events are processed without it. Using LoopController is recommended");
    }
#endif

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
            if (state->config.quitOnClose)
            {
              quit();
            }
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
            applyFullscreen(*state, state->isFullscreen ? FullscreenMode::windowed : FullscreenMode::borderless);
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

  void setEventsHandledByLoop(bool handled)
  {
    eventsHandledByLoop = handled;
  }

  bool areEventsHandledByLoop()
  {
    return eventsHandledByLoop;
  }
}

namespace Teller
{
  Window::Window(const WindowConfig& config)
    : state_(std::make_unique<Internal::WindowState>())
  {
    state_->config = config;
    state_->config.smoothRate = Internal::validSmoothRate(config.smoothRate);
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

    if (state_->gameWidth == 0)
    {
      state_->gameWidth = config.width.px();
      state_->gameHeight = config.height.px();
    }

    state_->renderer = SDL_CreateRenderer(state_->handle, nullptr);
    if (state_->renderer == nullptr)
    {
      Internal::logError("SDL_CreateRenderer");
    }
    else if (config.resizeMode == Resize::scale)
    {
      // ゲーム画面の座標のまま描くと、SDL3が拡大と黒い帯の処理を行う
      SDL_SetRenderLogicalPresentation(state_->renderer, state_->gameWidth, state_->gameHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    Internal::addDrawListener(*state_);

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

  void Window::destroy()
  {
    if (state_->handle == nullptr)
    {
      return;
    }
    // 再びcreate()できるようSDL_QuitSubSystemはデストラクタで行う
    Internal::closeWindow(*state_);
  }

  bool Window::isOpen() const
  {
    // 自前のループの条件で毎回呼ばれることを当てにする
    if (!Internal::eventsHandledByLoop)
    {
      Internal::deliverDraw();
      Internal::processEvents();
    }
    return state_->handle != nullptr;
  }

  void Window::subscribe(const Asset& asset)
  {
    const void* source = &asset;
    Internal::noteSubscribed(source);

    std::vector<const void*>& subscriptions = state_->subscriptions;
    if (std::find(subscriptions.begin(), subscriptions.end(), source) == subscriptions.end())
    {
      subscriptions.push_back(source);
    }
  }

  void Window::unsubscribe(const Asset& asset)
  {
    const void* source = &asset;
    std::erase(state_->subscriptions, source);
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

  Change Window::setWidth(Length width)
  {
    return Change(state_->widthTransition, width.px());
  }

  Change Window::setHeight(Length height)
  {
    return Change(state_->heightTransition, height.px());
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

  Change Window::setX(Length x)
  {
    return Change(state_->xTransition, x.px());
  }

  Change Window::setY(Length y)
  {
    return Change(state_->yTransition, y.px());
  }

  FrameRate Window::getSmoothRate() const
  {
    return Internal::effectiveSmoothFps(*state_);
  }

  void Window::setSmoothRate(FrameRate rate)
  {
    state_->config.smoothRate = Internal::validSmoothRate(rate);
  }

  void Window::resetSmoothRate()
  {
    state_->config.smoothRate.reset();
  }
}
