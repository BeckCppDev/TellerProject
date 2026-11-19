#include <Teller/Audio/Audio.hpp>

#include <Audio/AudioBus.hpp>
#include <Loop/ClockInternal.hpp>
#include <Teller/Asset/Asset.hpp>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

// SDL3_mixerのトラックにリサンプリングを任せると、リサンプラーのパディング分を読まずに出力する
// そのため再生開始の周期の末尾に無音が入り、ファイルの末尾の数フレームが失われる
// これを避けるため音ごとに次の資源を持ち、リサンプリングだけを自前で行う
// 1. ファイルと同じ周波数のミキサーとトラックで、リサンプリングせずにデコードする
// 2. SDL_AudioStreamで48000Hzへ変換する
//    必要な量より多く入れてから取り出し、終端ではフラッシュする
// 3. 48000Hzのミキサーのトラックへ渡し、ミキシングはSDL3_mixerで行う
// 番号は下のVoiceの項目に対応する
namespace Teller::Internal
{
  namespace
  {
    constexpr int outputFrequency = 48000;
    constexpr int outputChannels = 2;
    constexpr SDL_AudioSpec outputSpec{ SDL_AUDIO_F32, outputChannels, outputFrequency };
    constexpr int outputFrameBytes = static_cast<int>(sizeof(float)) * outputChannels;

    // 1回にデコードするフレーム数
    // 作る音はこの値に左右されない
    constexpr int decodeChunkFrames = 1024;

    // 出力機器のキューに保つ量を周期の数で表す
    // 少ないと途切れやすく、多いと遅延が増える
    constexpr double targetQueuedCycles = 2.0;

    // これより溜まったら、出力機器が止まっていたとみなして捨てる
    constexpr int maxQueuedCycles = 8;

    // クロックのずれを吸収する再生速度の調整の強さと上限
    constexpr double rateAdjustGain = 0.01;
    constexpr double maxRateAdjust = 0.005;

    void logError(const char* what)
    {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Audio: %s failed: %s", what, SDL_GetError());
    }

    // SDL3_mixerのストリームと同じく、オーディオの終了処理で自動では破棄させない
    SDL_AudioStream* createStream(const SDL_AudioSpec& input, const SDL_AudioSpec& output)
    {
      SDL_AudioStream* stream = SDL_CreateAudioStream(&input, &output);
      if (stream != nullptr)
      {
        SDL_SetBooleanProperty(SDL_GetAudioStreamProperties(stream), SDL_PROP_AUDIOSTREAM_AUTO_CLEANUP_BOOLEAN, false);
      }
      return stream;
    }

    // 1つのAudioで1つの音を鳴らすための資源
    struct Voice
    {
      Voice() = default;
      ~Voice();

      Voice(const Voice&) = delete;
      Voice& operator=(const Voice&) = delete;

      // 1
      MIX_Mixer* decoder = nullptr;
      MIX_Track* source = nullptr;

      // 2
      SDL_AudioStream* resampler = nullptr;

      // 3
      // 周期ごとにその周期の分だけを入れる
      SDL_AudioStream* output = nullptr;
      MIX_Track* track = nullptr;

      bool playedLastCycle = false;
      bool playedThisCycle = false;
      bool loop = false;

      // 最後までデコードしたらtrueになる
      bool sourceEnded = false;
    };

    Voice::~Voice()
    {
      // トラックが読んでいるストリームより先にトラックを消す
      if (track != nullptr)
      {
        MIX_DestroyTrack(track);
      }
      if (output != nullptr)
      {
        SDL_DestroyAudioStream(output);
      }
      if (resampler != nullptr)
      {
        SDL_DestroyAudioStream(resampler);
      }
      if (decoder != nullptr)
      {
        // ミキサーと一緒にそのトラックも消える
        MIX_DestroyMixer(decoder);
      }
    }

    struct Engine
    {
      bool libraryInitialized = false;

      // 全ての音を混ぜる48000Hzのミキサー
      MIX_Mixer* mixer = nullptr;

      SDL_AudioStream* device = nullptr;
      bool audioSubsystemInitialized = false;

      std::vector<AudioState*> audios;

      std::vector<float> mixed;
      std::vector<float> scratch;

      // 48000÷fpsの余りを繰り越し、割り切れないfpsでも1秒を48000フレームにする
      int frameRemainder = 0;

      // キューの量の移動平均で、負ならまだ測っていない
      double averageQueuedBytes = -1.0;
    };

    // グローバルのAssetとAudioのデストラクタから呼ばれるので、破棄順に左右されないよう解放しない
    Engine& engine()
    {
      static Engine* instance = new Engine;
      return *instance;
    }

    MIX_Mixer* mainMixer()
    {
      Engine& state = engine();
      if (state.mixer == nullptr && initMixerLibrary())
      {
        state.mixer = MIX_CreateMixer(&outputSpec);
        if (state.mixer == nullptr)
        {
          logError("MIX_CreateMixer");
        }
      }
      return state.mixer;
    }

    bool playTrack(MIX_Track* track, int loops, bool haltWhenExhausted)
    {
      const SDL_PropertiesID options = SDL_CreateProperties();
      SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
      SDL_SetBooleanProperty(options, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, haltWhenExhausted);
      const bool played = MIX_PlayTrack(track, options);
      SDL_DestroyProperties(options);
      return played;
    }

    // 失敗したときはログを出してnullptrを返す
    std::unique_ptr<Voice> createVoice(const SoundData& sound)
    {
      MIX_Mixer* mixer = mainMixer();
      if (mixer == nullptr)
      {
        return nullptr;
      }

      auto voice = std::make_unique<Voice>();
      const SDL_AudioSpec decodeSpec{ SDL_AUDIO_F32, outputChannels, sound.frequency };

      voice->decoder = MIX_CreateMixer(&decodeSpec);
      if (voice->decoder == nullptr)
      {
        logError("MIX_CreateMixer");
        return nullptr;
      }
      voice->source = MIX_CreateTrack(voice->decoder);
      if (voice->source == nullptr || !MIX_SetTrackAudio(voice->source, sound.audio))
      {
        logError("MIX_SetTrackAudio");
        return nullptr;
      }

      voice->resampler = createStream(decodeSpec, outputSpec);
      voice->output = createStream(outputSpec, outputSpec);
      if (voice->resampler == nullptr || voice->output == nullptr)
      {
        logError("SDL_CreateAudioStream");
        return nullptr;
      }

      voice->track = MIX_CreateTrack(mixer);
      if (voice->track == nullptr || !MIX_SetTrackAudioStream(voice->track, voice->output))
      {
        logError("MIX_SetTrackAudioStream");
        return nullptr;
      }

      // ストリームが空になっても止めず、次の周期に入れた分を続けて鳴らす
      if (!playTrack(voice->track, 0, false))
      {
        logError("MIX_PlayTrack");
        return nullptr;
      }
      return voice;
    }

    // 変換済みでまだ鳴らしていない分も捨てる
    void stopVoice(Voice& voice)
    {
      MIX_StopTrack(voice.source, 0);
      SDL_ClearAudioStream(voice.resampler);
      SDL_ClearAudioStream(voice.output);
      voice.sourceEnded = false;
    }

    void startVoice(Voice& voice, bool loop)
    {
      // MIX_PlayTrack()はシークするだけで、トラック内部のストリームに前の位置のデータを残す
      // MIX_StopTrack()はそれを捨てるので先に呼ぶ
      stopVoice(voice);
      if (!playTrack(voice.source, loop ? -1 : 0, true))
      {
        logError("MIX_PlayTrack");
        voice.sourceEnded = true;
      }
    }

    // その周期の分を48000Hzにして、混ぜる側のトラックへ渡す
    void feedVoice(Voice& voice, int frames, std::vector<float>& scratch)
    {
      const int neededBytes = frames * outputFrameBytes;
      const int chunkBytes = decodeChunkFrames * outputFrameBytes;
      scratch.resize(static_cast<std::size_t>(std::max(neededBytes, chunkBytes) / static_cast<int>(sizeof(float))));

      // 取り出せる量で判定するので、パディング分まで入れてから取り出すことになる
      while (!voice.sourceEnded && SDL_GetAudioStreamAvailable(voice.resampler) < neededBytes)
      {
        // このミキサーのトラックは1つなので、戻り値はデコードできた量になる
        const int generated = MIX_Generate(voice.decoder, scratch.data(), chunkBytes);
        if (generated > 0)
        {
          SDL_PutAudioStreamData(voice.resampler, scratch.data(), generated);
        }
        if (generated < chunkBytes)
        {
          // 終端なので、パディングとして残っている分もフラッシュして取り出す
          voice.sourceEnded = true;
          SDL_FlushAudioStream(voice.resampler);
        }
      }

      const int taken = SDL_GetAudioStreamData(voice.resampler, scratch.data(), neededBytes);
      if (taken > 0)
      {
        SDL_PutAudioStreamData(voice.output, scratch.data(), taken);
      }
    }

    int nextCycleFrames(Engine& state)
    {
      const int fps = clockFps() > 0 ? clockFps() : 30;
      state.frameRemainder += outputFrequency;
      const int frames = state.frameRemainder / fps;
      state.frameRemainder %= fps;
      return frames;
    }

    bool openOutputDevice()
    {
      Engine& state = engine();
      if (state.device != nullptr)
      {
        return true;
      }

      if (!state.audioSubsystemInitialized)
      {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
          logError("SDL_InitSubSystem");
          return false;
        }
        state.audioSubsystemInitialized = true;
      }

      state.device = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &outputSpec, nullptr, nullptr);
      if (state.device == nullptr)
      {
        logError("SDL_OpenAudioDeviceStream");
        return false;
      }
      state.averageQueuedBytes = -1.0;

      // 開いた直後は一時停止している
      SDL_ResumeAudioStreamDevice(state.device);
      return true;
    }

    void closeOutputDevice()
    {
      Engine& state = engine();
      if (state.device != nullptr)
      {
        SDL_DestroyAudioStream(state.device);
        state.device = nullptr;
      }
      if (state.audioSubsystemInitialized)
      {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        state.audioSubsystemInitialized = false;
      }
    }

    // 作った音には触れず、出力機器側の再生速度だけを調整するので、作る音は決定的なまま
    void sendToDevice(Engine& state, std::span<const float> samples)
    {
      if (state.device == nullptr || samples.empty())
      {
        return;
      }

      const int cycleBytes = static_cast<int>(samples.size_bytes());
      int queued = SDL_GetAudioStreamQueued(state.device);

      if (queued > cycleBytes * maxQueuedCycles)
      {
        SDL_ClearAudioStream(state.device);
        queued = 0;
      }

      // 空なら再生開始か処理落ちの後なので、1周期分の無音を先に入れてキューの余裕を作る
      // 作った音は欠けない
      if (queued == 0)
      {
        state.scratch.assign(samples.size(), 0.0f);
        SDL_PutAudioStreamData(state.device, state.scratch.data(), cycleBytes);
        state.averageQueuedBytes = -1.0;
      }

      SDL_PutAudioStreamData(state.device, samples.data(), cycleBytes);

      // キューの量が目安に近づくよう再生速度をわずかに変え、クロックのずれを吸収する
      const double queuedNow = SDL_GetAudioStreamQueued(state.device);
      state.averageQueuedBytes = state.averageQueuedBytes < 0.0 ? queuedNow : state.averageQueuedBytes * 0.95 + queuedNow * 0.05;
      const double target = cycleBytes * targetQueuedCycles;
      const double adjust = std::clamp((state.averageQueuedBytes - target) / target * rateAdjustGain, -maxRateAdjust, maxRateAdjust);
      SDL_SetAudioStreamFrequencyRatio(state.device, static_cast<float>(1.0 + adjust));
    }

#ifndef NDEBUG
    void warnIfNeverSubscribed(std::span<const PlayMessage> messages)
    {
      static bool warned = false;
      if (warned)
      {
        return;
      }
      for (const PlayMessage& message : messages)
      {
        if (!isAudioSubscribedByAny(message.sound->source))
        {
          warned = true;
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Sound: play() has no effect because no Audio has subscribed to its Asset");
          return;
        }
      }
    }
#endif
  }

  struct AudioState
  {
    AudioState();
    ~AudioState();

    AudioState(const AudioState&) = delete;
    AudioState& operator=(const AudioState&) = delete;

    void prepareCycle(std::span<const PlayMessage> messages, int frames, std::vector<float>& scratch);

    [[nodiscard]] bool isSubscribed(const void* source) const;

    std::vector<const void*> subscriptions;

    // 最初に鳴らすときに作り、音かAudioが消えるまで使い回す
    std::unordered_map<const SoundData*, std::unique_ptr<Voice>> voices;
  };

  AudioState::AudioState()
  {
    engine().audios.push_back(this);
  }

  AudioState::~AudioState()
  {
    voices.clear();
    Engine& state = engine();
    std::erase(state.audios, this);
    if (state.audios.empty())
    {
      closeOutputDevice();
    }
  }

  bool AudioState::isSubscribed(const void* source) const
  {
    return std::ranges::find(subscriptions, source) != subscriptions.end();
  }

  void AudioState::prepareCycle(std::span<const PlayMessage> messages, int frames, std::vector<float>& scratch)
  {
    for (auto& [sound, voice] : voices)
    {
      voice->playedThisCycle = false;
    }

    for (const PlayMessage& message : messages)
    {
      const SoundData* sound = message.sound.get();
      if (sound->audio == nullptr || !isSubscribed(sound->source))
      {
        continue;
      }

      auto found = voices.find(sound);
      if (found == voices.end())
      {
        std::unique_ptr<Voice> created = createVoice(*sound);
        if (created == nullptr)
        {
          continue;
        }
        found = voices.emplace(sound, std::move(created)).first;
      }
      Voice& voice = *found->second;

      // 前の周期に呼ばれていないか.fromStart()があれば最初から鳴らす
      if (!voice.playedLastCycle || message.fromStart)
      {
        startVoice(voice, message.loop);
      }
      else if (voice.loop != message.loop)
      {
        MIX_SetTrackLoops(voice.source, message.loop ? -1 : 0);
      }
      voice.loop = message.loop;
      voice.playedThisCycle = true;
      feedVoice(voice, frames, scratch);
    }

    for (auto& [sound, voice] : voices)
    {
      if (voice->playedLastCycle && !voice->playedThisCycle)
      {
        stopVoice(*voice);
      }
      voice->playedLastCycle = voice->playedThisCycle;
    }
  }

  bool initMixerLibrary()
  {
    Engine& state = engine();
    if (!state.libraryInitialized)
    {
      if (!MIX_Init())
      {
        logError("MIX_Init");
        return false;
      }
      state.libraryInitialized = true;
    }
    return true;
  }

  std::span<const float> generateAudioCycle(int frames)
  {
    Engine& state = engine();
    const std::span<const PlayMessage> messages = committedPlays();

#ifndef NDEBUG
    warnIfNeverSubscribed(messages);
#endif

    state.mixed.assign(static_cast<std::size_t>(frames) * outputChannels, 0.0f);

    // Audioがなければミキサーを作らない
    if (!state.audios.empty() && frames > 0)
    {
      for (AudioState* audio : state.audios)
      {
        audio->prepareCycle(messages, frames, state.scratch);
      }
      if (MIX_Mixer* mixer = mainMixer())
      {
        MIX_Generate(mixer, state.mixed.data(), frames * outputFrameBytes);
      }
    }

    clearPlays();
    return state.mixed;
  }

  void deliverAudio()
  {
    Engine& state = engine();
    const std::span<const float> samples = generateAudioCycle(nextCycleFrames(state));
    sendToDevice(state, samples);
  }

  void releaseSoundVoices(const SoundData* sound)
  {
    for (AudioState* audio : engine().audios)
    {
      audio->voices.erase(sound);
    }
  }

  void releaseSourceSubscriptions(const void* source)
  {
    for (AudioState* audio : engine().audios)
    {
      std::erase(audio->subscriptions, source);
    }
  }
}

namespace Teller
{
  Audio::Audio()
    : state_(std::make_unique<Internal::AudioState>())
  {
  }

  Audio::~Audio() = default;

  void Audio::subscribe(const Asset& asset)
  {
    const void* source = &asset;
    Internal::noteAudioSubscribed(source);
    if (!state_->isSubscribed(source))
    {
      state_->subscriptions.push_back(source);
    }
    Internal::openOutputDevice();
  }

  void Audio::unsubscribe(const Asset& asset)
  {
    const void* source = &asset;
    std::erase(state_->subscriptions, source);
  }
}
