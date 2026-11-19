#pragma once

#include <Teller/Asset/AssetPath.hpp>

#include <memory>

namespace Teller
{
  class Asset;
  class Sound;

  namespace Internal
  {
    struct SoundData;

    // Sound::play()の戻り値で、.loop()と.fromStart()を続けて書くためだけに使う
    class PlayRequest
    {
    public:
      // 鳴らせない音のときはnullptrで、.loop()などは何もしない
      explicit PlayRequest(const SoundData* sound) noexcept;

      PlayRequest(const PlayRequest&) = delete;
      PlayRequest& operator=(const PlayRequest&) = delete;
      PlayRequest(PlayRequest&&) = delete;
      PlayRequest& operator=(PlayRequest&&) = delete;

      // 呼び続ける間は、最後まで鳴ったら最初に戻って繰り返す
      PlayRequest& loop();

      // 前の周期から鳴り続けていても、この周期は最初から鳴らす
      PlayRequest& fromStart();

    private:
      const SoundData* sound_;
    };
  }

  // 指定する項目は宣言の順番どおりに並べる
  struct SoundConfig
  {
    AssetPath path;
  };

  // Asset::audio()が返す音
  // 実体はAssetが持ち、コピーしても同じ音を指す
  // 何も指定していないSoundとAssetの破棄後は、play()が何も鳴らさない
  // 同じ音を同時に重ねて鳴らすことはできない
  // コピーしたSoundと、同じパスを指定したSoundも同じ音として扱う
  // 1周期に何回play()を呼んでも1回分になる
  class Sound
  {
  public:
    Sound() = default;

    // 読み込み済みなら何もしない
    // 失敗したときはログを出してfalseを返し、次の呼び出しで読み直す
    bool load() const;

    // 呼び続ける間は続きを鳴らし、呼ばなかった周期で止まる
    // 止まった後に呼ぶと最初から鳴る
    // .loop()がなければ、最後まで鳴った後は呼び続けても鳴らない
    // 読み込んでいなければ初回だけ読む
    // Asset::update()で確定するまで鳴らない
    Internal::PlayRequest play() const;

  private:
    friend class Asset;

    explicit Sound(const std::shared_ptr<Internal::SoundData>& data);

    // Assetの破棄を検出するため、延命しない弱い参照で持つ
    std::weak_ptr<Internal::SoundData> data_;
  };
}
