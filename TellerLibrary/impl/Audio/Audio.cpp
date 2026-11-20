#include <Teller/Audio/Audio.hpp>

#include <Audio/AudioBus.hpp>
#include <Loop/ClockInternal.hpp>
#include <Smooth/SmoothRate.hpp>
#include <Smooth/Transition.hpp>
#include <Teller/Asset/Asset.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// SDL3_mixerのトラックにリサンプリングを任せると、リサンプラーのパディング分を読まずに出力する
// そのため再生開始の周期の末尾に無音が入り、ファイルの末尾の数フレームが失われる
// これを避けるため次の資源を持ち、リサンプリングだけを自前で行う
// 1. ファイルと同じ周波数のミキサーとトラックで、リサンプリングせずにデコードする
//    .loop()の繰り返しもここで行い、ファイル1回分の長さを数える
// 2. SDL_AudioStreamで48000Hzへ変換する
//    必要な量より多く入れてから取り出し、終端ではフラッシュする
// 3. 48000Hzのミキサーのトラックへ渡し、ミキシングはSDL3_mixerで行う
// 1と2は音ごとのPlaybackで、再生位置もここで数えるのでAudioの購読に左右されない
// 3はAudioと音の組ごとのVoiceで、Audioの音量はVoiceへ渡す前に掛ける
namespace Teller::Internal
{
  namespace
  {
    constexpr int outputFrequency = 48000;
    constexpr int outputChannels = 2;
    constexpr SDL_AudioSpec outputSpec{ SDL_AUDIO_F32, outputChannels, outputFrequency };

    // デコードもステレオで行うので、1フレームの大きさはどちらも同じ
    constexpr int frameBytes = static_cast<int>(sizeof(float)) * outputChannels;

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

    constexpr std::int64_t microsecondsPerSecond = 1'000'000;

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

    // 1と2
    // 現状の問題点：音1つにつき1つしか持たないので、同じ音を重ねて鳴らせない
    struct Playback
    {
      Playback() = default;
      ~Playback();

      Playback(const Playback&) = delete;
      Playback& operator=(const Playback&) = delete;

      // 1
      MIX_Mixer* decoder = nullptr;
      MIX_Track* source = nullptr;

      // 2
      SDL_AudioStream* resampler = nullptr;

      // この周期の分で、購読している全てのAudioが使う
      std::vector<float> cycle;

      bool playedLastCycle = false;
      bool playedThisCycle = false;
      bool loop = false;

      // 最後までデコードし、先頭に戻らなかったらtrue
      bool sourceEnded = false;

      // 今の1回分でデコードした、ファイルの周波数のフレーム数
      std::int64_t decodedFrames = 0;

      // ファイルの周波数のフレーム数で、最初に最後までデコードするまでは-1
      // mp3はMIX_GetAudioDuration()が実際より長い値を返すので、デコードした量で数える
      std::int64_t sourceLength = -1;

      // 鳴らし始めてから、デコードが先頭に戻った回数
      std::int64_t completedPasses = 0;

      // 鳴らし始めてから取り出した48000Hzのフレーム数
      std::int64_t playedFrames = 0;

      PlaybackStatus status;
    };

    Playback::~Playback()
    {
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

    // 3
    struct Voice
    {
      Voice() = default;
      ~Voice();

      Voice(const Voice&) = delete;
      Voice& operator=(const Voice&) = delete;

      // 周期ごとにその周期の分だけを入れる
      SDL_AudioStream* output = nullptr;
      MIX_Track* track = nullptr;
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
    }

    struct Engine
    {
      bool libraryInitialized = false;

      // 全ての音を混ぜる48000Hzのミキサー
      MIX_Mixer* mixer = nullptr;

      SDL_AudioStream* device = nullptr;
      bool audioSubsystemInitialized = false;

      std::vector<AudioState*> audios;

      // 最初に鳴らすときに作り、音が消えるまで使い回す
      std::unordered_map<const SoundData*, std::unique_ptr<Playback>> playbacks;

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
    std::unique_ptr<Playback> createPlayback(const SoundData& sound)
    {
      auto playback = std::make_unique<Playback>();
      const SDL_AudioSpec decodeSpec{ SDL_AUDIO_F32, outputChannels, sound.frequency };

      playback->decoder = MIX_CreateMixer(&decodeSpec);
      if (playback->decoder == nullptr)
      {
        logError("MIX_CreateMixer");
        return nullptr;
      }
      playback->source = MIX_CreateTrack(playback->decoder);
      if (playback->source == nullptr || !MIX_SetTrackAudio(playback->source, sound.audio))
      {
        logError("MIX_SetTrackAudio");
        return nullptr;
      }

      playback->resampler = createStream(decodeSpec, outputSpec);
      if (playback->resampler == nullptr)
      {
        logError("SDL_CreateAudioStream");
        return nullptr;
      }
      return playback;
    }

    // 変換済みでまだ鳴らしていない分も捨てる
    void stopPlayback(Playback& playback)
    {
      MIX_StopTrack(playback.source, 0);
      SDL_ClearAudioStream(playback.resampler);
      playback.sourceEnded = false;
      playback.decodedFrames = 0;
      playback.completedPasses = 0;
      playback.playedFrames = 0;
    }

    void startPlayback(Playback& playback)
    {
      // MIX_PlayTrack()はシークするだけで、トラック内部のストリームに前の位置のデータを残す
      // MIX_StopTrack()はそれを捨てるので先に呼ぶ
      stopPlayback(playback);

      // 再生位置を数えるため、繰り返しはSDL3_mixerに任せずfillResampler()で先頭へ戻す
      if (!playTrack(playback.source, 0, true))
      {
        logError("MIX_PlayTrack");
        playback.sourceEnded = true;
      }
    }

    // 48000Hzで取り出せる量がneededBytes以上になるか、最後まで読み出すまでデコードする
    void fillResampler(Playback& playback, int neededBytes, std::vector<float>& scratch)
    {
      const int chunkBytes = decodeChunkFrames * frameBytes;
      scratch.resize(static_cast<std::size_t>(chunkBytes / static_cast<int>(sizeof(float))));

      // 取り出せる量で判定するので、パディング分まで入れてから取り出すことになる
      while (!playback.sourceEnded && SDL_GetAudioStreamAvailable(playback.resampler) < neededBytes)
      {
        // このミキサーのトラックは1つなので、戻り値はデコードできた量になる
        const int generated = MIX_Generate(playback.decoder, scratch.data(), chunkBytes);
        if (generated > 0)
        {
          SDL_PutAudioStreamData(playback.resampler, scratch.data(), generated);
          playback.decodedFrames += generated / frameBytes;
        }
        if (generated >= chunkBytes)
        {
          continue;
        }

        playback.sourceLength = playback.decodedFrames;

        // リサンプラーをフラッシュせずに先頭からのデコードを続けて入れるので、つなぎ目に無音が入らない
        // 1フレームも読めなかったときは無限に繰り返さないよう終える
        if (playback.loop && playback.decodedFrames > 0 && playTrack(playback.source, 0, true))
        {
          ++playback.completedPasses;
          playback.decodedFrames = 0;
        }
        else
        {
          // パディングとして残っている分も取り出せるようにする
          playback.sourceEnded = true;
          SDL_FlushAudioStream(playback.resampler);
        }
      }
    }

    // その周期の分を48000Hzにしてplayback.cycleに入れる
    void decodeCycle(Playback& playback, int frames, std::vector<float>& scratch)
    {
      const int neededBytes = frames * frameBytes;
      fillResampler(playback, neededBytes, scratch);

      playback.cycle.assign(static_cast<std::size_t>(frames) * outputChannels, 0.0f);
      const int taken = SDL_GetAudioStreamData(playback.resampler, playback.cycle.data(), neededBytes);
      if (taken > 0)
      {
        playback.playedFrames += taken / frameBytes;
      }

      // 次の1フレームまでデコードしておき、長さが周期の整数倍の音でもisFinished()が1周期遅れないようにする
      fillResampler(playback, frameBytes, scratch);
    }

    // 鳴らした量からSound::getPosition()とSound::isFinished()の値を求める
    // 整数だけで計算するので、TASで再生しても同じ値になる
    void updateStatus(Playback& playback, int frequency)
    {
      const bool drained = playback.sourceEnded && SDL_GetAudioStreamAvailable(playback.resampler) <= 0;
      const std::int64_t played = playback.playedFrames * microsecondsPerSecond / outputFrequency;
      playback.status.finished = drained;

      if (playback.sourceLength < 0)
      {
        playback.status.positionMicroseconds = played;
        return;
      }

      const std::int64_t length = playback.sourceLength * microsecondsPerSecond / frequency;
      if (drained || length <= 0)
      {
        playback.status.positionMicroseconds = length;
        return;
      }

      // デコードは鳴らす位置より先に進むので、先頭に戻った回数のうち鳴らす位置が既に越えた分だけ引く
      const std::int64_t wrapped = std::min(playback.completedPasses, played / length);
      playback.status.positionMicroseconds = played - wrapped * length;
    }

    // Audioの購読によらず、play()を呼んだ音は全てこの周期の分を読み出す
    void advancePlaybacks(Engine& state, std::span<const PlayMessage> messages, int frames)
    {
      for (auto& [sound, playback] : state.playbacks)
      {
        playback->playedThisCycle = false;
      }

      for (const PlayMessage& message : messages)
      {
        const SoundData& sound = *message.sound;
        if (sound.audio == nullptr)
        {
          continue;
        }

        auto found = state.playbacks.find(&sound);
        if (found == state.playbacks.end())
        {
          std::unique_ptr<Playback> created = createPlayback(sound);
          if (created == nullptr)
          {
            continue;
          }
          found = state.playbacks.emplace(&sound, std::move(created)).first;
        }
        Playback& playback = *found->second;

        // 前の周期に呼ばれていないか.fromStart()があれば最初から鳴らす
        if (!playback.playedLastCycle || message.fromStart)
        {
          startPlayback(playback);
        }
        playback.loop = message.loop;
        playback.playedThisCycle = true;
        decodeCycle(playback, frames, state.scratch);
        updateStatus(playback, sound.frequency);
      }

      // 呼ばれなかった音は止め、位置を0に戻す
      for (auto& [sound, playback] : state.playbacks)
      {
        if (!playback->playedThisCycle)
        {
          if (playback->playedLastCycle)
          {
            stopPlayback(*playback);
          }
          playback->status = {};
        }
        playback->playedLastCycle = playback->playedThisCycle;
      }
    }

    // この周期に読み出した音を返す
    // 呼ばれなかった音と読み出せなかった音はnullptr
    const Playback* playedPlayback(const SoundData* sound)
    {
      const auto& playbacks = engine().playbacks;
      const auto found = playbacks.find(sound);
      if (found == playbacks.end() || !found->second->playedThisCycle)
      {
        return nullptr;
      }
      return found->second.get();
    }

    // 失敗したときはログを出してnullptrを返す
    std::unique_ptr<Voice> createVoice()
    {
      MIX_Mixer* mixer = mainMixer();
      if (mixer == nullptr)
      {
        return nullptr;
      }

      auto voice = std::make_unique<Voice>();
      voice->output = createStream(outputSpec, outputSpec);
      if (voice->output == nullptr)
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

    // この周期に読み出した音のうち購読している分に音量を掛け、混ぜる側のトラックへ渡す
    void mixCycle(std::span<const PlayMessage> messages, int frames, std::vector<float>& scratch);

    [[nodiscard]] bool isSubscribed(const void* source) const;

    std::vector<const void*> subscriptions;

    // 最初に鳴らすときに作り、音かAudioが消えるまで使い回す
    std::unordered_map<const SoundData*, std::unique_ptr<Voice>> voices;

    // 0から100の%
    double volume = 100.0;

    bool muted = false;

    // mute()で1.0から0.0の間を動く倍率で、volumeとは別に持つ
    double muteLevel = 1.0;

    // 前の周期の終わりに掛けた倍率
    // 次の周期はここから1フレームずつ変え、急な変化でクリックノイズが出ないようにする
    // 負なら1周期も作っておらず、最初の周期は変化させない
    double appliedGain = -1.0;

    SmoothRateSetting smoothRate{ "Teller::Audio" };

    Transition volumeTransition;
    Transition muteTransition;

    // 範囲外の音量に返す、何も変えない変化
    Transition ignoredTransition;
  };

  AudioState::AudioState()
    : volumeTransition({
        .read = [this] { return volume; },
        .apply = [this](double value) { volume = value; },
        .canSmooth = [] { return true; },
        .fps = [this] { return smoothRate.fps(); },
      })
    , muteTransition({
        .read = [this] { return muteLevel; },
        .apply = [this](double value) { muteLevel = value; },
        .canSmooth = [] { return true; },
        .fps = [this] { return smoothRate.fps(); },
      })
    , ignoredTransition({
        .read = [this] { return volume; },
        .apply = [](double) {},
        // .smooth()もすぐ終わる
        .canSmooth = [] { return false; },
        .fps = [this] { return smoothRate.fps(); },
      })
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

  void AudioState::mixCycle(std::span<const PlayMessage> messages, int frames, std::vector<float>& scratch)
  {
    const double endGain = volume / 100.0 * muteLevel;
    const double startGain = appliedGain < 0.0 ? endGain : appliedGain;
    appliedGain = endGain;

    for (const PlayMessage& message : messages)
    {
      const SoundData* sound = message.sound.get();
      if (!isSubscribed(sound->source))
      {
        continue;
      }
      const Playback* playback = playedPlayback(sound);
      if (playback == nullptr)
      {
        continue;
      }

      auto found = voices.find(sound);
      if (found == voices.end())
      {
        std::unique_ptr<Voice> created = createVoice();
        if (created == nullptr)
        {
          continue;
        }
        found = voices.emplace(sound, std::move(created)).first;
      }

      // 倍率は1フレームずつ線形に変える
      scratch.resize(playback->cycle.size());
      for (int frame = 0; frame < frames; ++frame)
      {
        const double gain = startGain + (endGain - startGain) * (frame + 1) / frames;
        for (int channel = 0; channel < outputChannels; ++channel)
        {
          const std::size_t at = static_cast<std::size_t>(frame) * outputChannels + channel;
          scratch[at] = static_cast<float>(playback->cycle[at] * gain);
        }
      }
      SDL_PutAudioStreamData(found->second->output, scratch.data(), frames * frameBytes);
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

  PlaybackStatus playbackStatus(const SoundData* sound)
  {
    const auto& playbacks = engine().playbacks;
    const auto found = playbacks.find(sound);
    return found != playbacks.end() ? found->second->status : PlaybackStatus{};
  }

  std::span<const float> generateAudioCycle(int frames)
  {
    Engine& state = engine();
    const std::span<const PlayMessage> messages = committedPlays();

#ifndef NDEBUG
    warnIfNeverSubscribed(messages);
#endif

    state.mixed.assign(static_cast<std::size_t>(frames) * outputChannels, 0.0f);

    if (frames > 0)
    {
      advancePlaybacks(state, messages, frames);

      // Audioがなければミキサーを作らない
      if (!state.audios.empty())
      {
        if (MIX_Mixer* mixer = mainMixer())
        {
          for (AudioState* audio : state.audios)
          {
            audio->mixCycle(messages, frames, state.scratch);
          }
          MIX_Generate(mixer, state.mixed.data(), frames * frameBytes);
        }
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
    Engine& state = engine();
    for (AudioState* audio : state.audios)
    {
      audio->voices.erase(sound);
    }
    state.playbacks.erase(sound);
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

  double Audio::getVolume() const
  {
    return state_->volume;
  }

  Change Audio::setVolume(double percent)
  {
    // NaNも弾くよう、範囲内かどうかを否定する形で比べる
    if (!(percent >= 0.0 && percent <= 100.0))
    {
#ifndef NDEBUG
      static bool warned = false;
      if (!warned)
      {
        warned = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Audio: volume must be between 0 and 100, so %g is ignored", percent);
      }
#endif
      return Change(state_->ignoredTransition, state_->volume);
    }
    return Change(state_->volumeTransition, percent);
  }

  bool Audio::isMuted() const
  {
    return state_->muted;
  }

  Change Audio::mute(bool muted)
  {
    state_->muted = muted;
    return Change(state_->muteTransition, muted ? 0.0 : 1.0);
  }

  FrameRate Audio::getSmoothRate() const
  {
    return state_->smoothRate.fps();
  }

  void Audio::setSmoothRate(FrameRate rate)
  {
    state_->smoothRate.set(rate);
  }

  void Audio::resetSmoothRate()
  {
    state_->smoothRate.reset();
  }
}
