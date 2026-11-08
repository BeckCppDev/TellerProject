#pragma once

#include <Teller/Window/Window.hpp>

#include <Graphics/DrawBus.hpp>
#include <Smooth/Transition.hpp>

#include <SDL3/SDL.h>

#include <unordered_map>
#include <vector>

namespace Teller::Internal
{
  // 通り道の受け取り手として、購読している送り主の内容だけを描く
  struct WindowState final : DrawListener
  {
    WindowState();

    void onFrame(std::span<const DrawMessage> messages) override;
    void onImageReleased(const ImageData* image) override;
    void onSourceReleased(const void* source) override;

    WindowConfig config;
    SDL_Window* handle = nullptr;
    SDL_Renderer* renderer = nullptr;

    // 最初のcreate()の時点の大きさをゲーム画面の大きさとして固定する
    // setWidth()などはウインドウの大きさだけを変える
    int gameWidth = 0;
    int gameHeight = 0;

    std::vector<const void*> subscriptions;

    // GPU上の画像
    // 実体はレンダラーに属するので、レンダラーの破棄とともに消える
    std::unordered_map<const ImageData*, SDL_Texture*> textures;

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
