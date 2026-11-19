#pragma once

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <filesystem>
#include <memory>
#include <span>

// 鳴らす指示の通り道と、音を作る処理の入口
// 送り主はAudioを知らずに流し、Audioは購読している送り主の音だけを鳴らす
// play()の指示は送り主のupdate()で確定し、LoopController::update()のたびに1周期分の音になる
namespace Teller::Internal
{
  struct SoundData
  {
    SoundData(const void* source, std::filesystem::path writtenPath, std::filesystem::path sourceDirectory);
    ~SoundData();

    SoundData(const SoundData&) = delete;
    SoundData& operator=(const SoundData&) = delete;

    // 送り主の識別にだけ使う
    const void* source;

    std::filesystem::path writtenPath;
    std::filesystem::path sourceDirectory;

    // 読み込むまではnullptr
    // ファイル全体を展開せず、鳴らす分だけデコードする
    MIX_Audio* audio = nullptr;

    // ファイルのサンプリング周波数
    int frequency = 0;

    // play()からの自動の読み込みを1回に限る
    bool loadAttempted = false;
  };

  // 失敗したときはログを出してfalseを返す
  bool loadSound(SoundData& sound);

  // 同じ音への指示は1周期に1つにまとめる
  struct PlayMessage
  {
    // 送り主が破棄されても周期の終わりまで音を保つ
    std::shared_ptr<const SoundData> sound;

    bool loop = false;
    bool fromStart = false;
  };

  void publishPlay(const std::shared_ptr<const SoundData>& sound);
  void markPlayLoop(const SoundData* sound);
  void markPlayFromStart(const SoundData* sound);

  // sourceの指示を確定した指示の末尾へ移す
  void commitPlay(const void* source);

  [[nodiscard]] std::span<const PlayMessage> committedPlays();

  // 確定した指示を、確定しなかった指示とともに捨てる
  void clearPlays();

  // 購読忘れの警告にだけ使う
  void noteAudioSubscribed(const void* source);
  [[nodiscard]] bool isAudioSubscribedByAny(const void* source);

  void notifyAudioSourceReleased(const void* source);

  // 以下はAudio.cppで定義する

  // 失敗したときはログを出してfalseを返す
  bool initMixerLibrary();

  // 確定した指示から1周期分の音を作り、指示を捨てる
  // 戻り値は48000Hzのステレオで、左右を交互に並べる
  // 出力機器には送らない
  std::span<const float> generateAudioCycle(int frames);

  // LoopController::update()が呼ぶ
  void deliverAudio();

  // LoopController::update()が呼ばれていない間に、isOpen()と同梱のmain()が呼ぶ
  // 音を作らずに指示を捨てる
  void discardAudio();

  // 音や送り主が消える前に、Audioが持つ関連の資源を片付ける
  void releaseSoundVoices(const SoundData* sound);
  void releaseSourceSubscriptions(const void* source);
}
