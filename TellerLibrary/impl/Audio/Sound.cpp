#include <Teller/Asset/Sound.hpp>

#include <Asset/AssetFiles.hpp>
#include <Audio/AudioBus.hpp>

#include <string>

namespace Teller::Internal
{
  bool loadSound(SoundData& sound)
  {
    sound.loadAttempted = true;
    if (sound.audio != nullptr)
    {
      return true;
    }
    if (!initMixerLibrary())
    {
      return false;
    }

    const std::string resolved = resolveAssetPath(sound.writtenPath, sound.sourceDirectory);

    // 長い曲でメモリを使いすぎないよう、ファイル全体を展開しない
    // 現状の問題点：リリースビルドでもファイルから読み、実行ファイルに埋め込まない
    sound.audio = MIX_LoadAudio(nullptr, resolved.c_str(), false);
    if (sound.audio == nullptr)
    {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Sound: failed to load %s: %s", resolved.c_str(), SDL_GetError());
      return false;
    }

    SDL_AudioSpec spec;
    if (!MIX_GetAudioFormat(sound.audio, &spec))
    {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Sound: failed to read the format of %s: %s", resolved.c_str(), SDL_GetError());
      MIX_DestroyAudio(sound.audio);
      sound.audio = nullptr;
      return false;
    }
    sound.frequency = spec.freq;
    return true;
  }
}

namespace Teller
{
  Internal::PlayRequest::PlayRequest(const SoundData* sound) noexcept
    : sound_(sound)
  {
  }

  Internal::PlayRequest& Internal::PlayRequest::loop()
  {
    if (sound_ != nullptr)
    {
      Internal::markPlayLoop(sound_);
    }
    return *this;
  }

  Internal::PlayRequest& Internal::PlayRequest::fromStart()
  {
    if (sound_ != nullptr)
    {
      Internal::markPlayFromStart(sound_);
    }
    return *this;
  }

  Sound::Sound(const std::shared_ptr<Internal::SoundData>& data)
    : data_(data)
  {
  }

  bool Sound::load() const
  {
    const std::shared_ptr<Internal::SoundData> data = data_.lock();
    if (data == nullptr)
    {
      return false;
    }
    return Internal::loadSound(*data);
  }

  Internal::PlayRequest Sound::play() const
  {
    const std::shared_ptr<Internal::SoundData> data = data_.lock();
    if (data == nullptr)
    {
#ifndef NDEBUG
      static bool warned = false;
      if (!warned)
      {
        warned = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Sound: play() was called on a sound that was not given by Asset::audio() or whose Asset was destroyed");
      }
#endif
      return Internal::PlayRequest(nullptr);
    }

    // 毎周期読み直してログが溢れないよう、自動で読むのは初回だけにする
    if (data->audio == nullptr)
    {
      if (data->loadAttempted || !Internal::loadSound(*data))
      {
        return Internal::PlayRequest(nullptr);
      }
    }
    Internal::publishPlay(data);
    return Internal::PlayRequest(data.get());
  }

  // 何も指定していないSoundとAssetの破棄後は0とfalseを返す
  Duration Sound::getPosition() const
  {
    const std::shared_ptr<Internal::SoundData> data = data_.lock();
    if (data == nullptr)
    {
      return Duration::fromMicroseconds(0);
    }
    return Duration::fromMicroseconds(Internal::playbackStatus(data.get()).positionMicroseconds);
  }

  bool Sound::isFinished() const
  {
    const std::shared_ptr<Internal::SoundData> data = data_.lock();
    return data != nullptr && Internal::playbackStatus(data.get()).finished;
  }
}
