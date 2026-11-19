#include <Audio/AudioBus.hpp>

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Teller::Internal
{
  namespace
  {
    struct Bus
    {
      std::vector<PlayMessage> pending;
      std::vector<PlayMessage> committed;
      std::unordered_set<const void*> subscribedSources;
    };

    // グローバルのAssetとAudioのデストラクタから呼ばれるので、破棄順に左右されないよう解放しない
    Bus& bus()
    {
      static Bus* instance = new Bus;
      return *instance;
    }

    PlayMessage* findMessage(std::vector<PlayMessage>& messages, const SoundData* sound)
    {
      const auto found = std::ranges::find_if(messages, [sound](const PlayMessage& message)
      {
        return message.sound.get() == sound;
      });
      return found != messages.end() ? &*found : nullptr;
    }
  }

  SoundData::SoundData(const void* source, std::filesystem::path writtenPath, std::filesystem::path sourceDirectory)
    : source(source)
    , writtenPath(std::move(writtenPath))
    , sourceDirectory(std::move(sourceDirectory))
  {
  }

  // MIX_Audioを読んでいるトラックを先に消す
  SoundData::~SoundData()
  {
    releaseSoundVoices(this);
    if (audio != nullptr)
    {
      MIX_DestroyAudio(audio);
    }
  }

  void publishPlay(const std::shared_ptr<const SoundData>& sound)
  {
    std::vector<PlayMessage>& pending = bus().pending;
    if (findMessage(pending, sound.get()) == nullptr)
    {
      pending.push_back({ sound });
    }
  }

  // play()の直後に呼ばれるので、確定前の指示に印を付ける
  void markPlayLoop(const SoundData* sound)
  {
    if (PlayMessage* message = findMessage(bus().pending, sound))
    {
      message->loop = true;
    }
  }

  void markPlayFromStart(const SoundData* sound)
  {
    if (PlayMessage* message = findMessage(bus().pending, sound))
    {
      message->fromStart = true;
    }
  }

  // update()を1周期に2回呼んでも、同じ音の指示は1つにまとめる
  void commitPlay(const void* source)
  {
    Bus& state = bus();
    for (PlayMessage& message : state.pending)
    {
      if (message.sound->source != source)
      {
        continue;
      }
      if (PlayMessage* existing = findMessage(state.committed, message.sound.get()))
      {
        existing->loop = existing->loop || message.loop;
        existing->fromStart = existing->fromStart || message.fromStart;
      }
      else
      {
        state.committed.push_back(std::move(message));
      }
    }
    std::erase_if(state.pending, [source](const PlayMessage& message)
    {
      // 移した指示はsoundが空になっている
      return message.sound == nullptr || message.sound->source == source;
    });
  }

  std::span<const PlayMessage> committedPlays()
  {
    return bus().committed;
  }

  void clearPlays()
  {
    Bus& state = bus();
    state.committed.clear();
    state.pending.clear();
  }

  void noteAudioSubscribed(const void* source)
  {
    bus().subscribedSources.insert(source);
  }

  bool isAudioSubscribedByAny(const void* source)
  {
    return bus().subscribedSources.contains(source);
  }

  void notifyAudioSourceReleased(const void* source)
  {
    bus().subscribedSources.erase(source);
    releaseSourceSubscriptions(source);
  }

  void discardAudio()
  {
#ifndef NDEBUG
    static bool warned = false;
    if (!warned && !bus().committed.empty())
    {
      warned = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Sound: play() has no effect without LoopController::update()");
    }
#endif
    clearPlays();
  }
}
