#pragma once

#include <Teller/Smooth/Change.hpp>
#include <Teller/Units/FrameRate.hpp>

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
  // 音量とmute()は、このAudioで鳴らす全ての音にかかる
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

    // その周期から聞こえなくなる
    // 再生位置はplay()を呼んでいる限り進み続けるので、購読し直すと続きから聞こえる
    virtual void unsubscribe(const Asset& asset);

    // 0から100の%で、最初は100
    // .smooth()で変えている途中は途中の値を返す
    [[nodiscard]] virtual double getVolume() const;

    // 小数も受け付ける
    // 範囲外の値は無視して前の音量のままにし、デバッグビルドで1度だけ警告する
    virtual Change setVolume(double percent);

    // 最後にmute()に渡した値を返す
    [[nodiscard]] virtual bool isMuted() const;

    // 音量の設定は変えずに無音にする
    virtual Change mute(bool muted);

    // 指定していないときはlogicの値を返す
    [[nodiscard]] virtual FrameRate getSmoothRate() const;

    // 次に始める変化から使う
    virtual void setSmoothRate(FrameRate rate);

    // logicと同じに戻す
    virtual void resetSmoothRate();

  private:
    std::unique_ptr<Internal::AudioState> state_;
  };
}
