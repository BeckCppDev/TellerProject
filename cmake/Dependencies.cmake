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
