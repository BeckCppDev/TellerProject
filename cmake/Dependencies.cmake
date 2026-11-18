include(FetchContent)

# SDL3は静的リンクする
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)

# 更新時はURLとURL_HASHの両方を書き換える
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.zip
  URL_HASH SHA256=9cd42377704398796071b8597cd7e21da254a43bad98c4199739647adc13fa6f
  SYSTEM
)
FetchContent_MakeAvailable(SDL3)

# SDL3_mixerも静的リンクする
# 読む形式はogg、mp3、wav、flacに限り、ライセンスの表示を求めない読み込み処理だけを使う
# 他の読み込み処理には表示を求める外部のライブラリを使うものがあるので全て切る
# BUILD_SHARED_LIBSを自前のターゲットに漏らさないためblock()で囲む
block()
  set(BUILD_SHARED_LIBS OFF)
  set(SDLMIXER_VENDORED OFF)
  set(SDLMIXER_TESTS OFF)
  set(SDLMIXER_EXAMPLES OFF)
  set(SDLMIXER_INSTALL OFF)

  # wavは内蔵の処理で読む
  set(SDLMIXER_WAVE ON)
  set(SDLMIXER_AIFF OFF)
  set(SDLMIXER_VOC OFF)
  set(SDLMIXER_AU OFF)

  # oggはstb_vorbis
  set(SDLMIXER_VORBIS_STB ON)
  set(SDLMIXER_VORBIS_VORBISFILE OFF)
  set(SDLMIXER_VORBIS_TREMOR OFF)

  # mp3はdr_mp3
  set(SDLMIXER_MP3 ON)
  set(SDLMIXER_MP3_DRMP3 ON)
  set(SDLMIXER_MP3_MPG123 OFF)

  # flacはdr_flac
  set(SDLMIXER_FLAC ON)
  set(SDLMIXER_FLAC_DRFLAC ON)
  set(SDLMIXER_FLAC_LIBFLAC OFF)

  set(SDLMIXER_OPUS OFF)
  set(SDLMIXER_MIDI OFF)
  set(SDLMIXER_MOD OFF)
  set(SDLMIXER_GME OFF)
  set(SDLMIXER_WAVPACK OFF)

  # 更新時はURLとURL_HASHの両方を書き換え、増えた読み込み処理があれば切る
  FetchContent_Declare(SDL3_mixer
    URL https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-3.2.4.zip
    URL_HASH SHA256=bbf0173861d5ee66555605435d4f423261d228649cb03dcb6cf3d24063683625
    SYSTEM
  )
  FetchContent_MakeAvailable(SDL3_mixer)
endblock()
