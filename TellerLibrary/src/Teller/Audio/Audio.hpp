#pragma once

#include <memory>

namespace Teller
{
  class Asset;

  namespace Internal
  {
    struct AudioState;
  }

  // 購読したAssetの音を鳴らす
  // 音はLoopController::update()のたびに1周期分ずつ作るので、LoopControllerが要る
  class Audio
  {
  public:
    Audio();
    virtual ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // 最初の購読で出力機器を開く
    // 重ねて購読しても1回分として扱う
    virtual void subscribe(const Asset& asset);

    // 鳴っている途中で外すと、その周期で止まる
    virtual void unsubscribe(const Asset& asset);

  private:
    std::unique_ptr<Internal::AudioState> state_;
  };
}
