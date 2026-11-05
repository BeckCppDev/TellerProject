add_library(TellerLibrary STATIC)

# src/は公開ヘッダー
target_sources(TellerLibrary
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS src
    FILES
      src/Teller.hpp
      src/Teller/Loop/LoopController.hpp
      src/Teller/Loop/Quit.hpp
      src/Teller/Loop/Timer.hpp
      src/Teller/Units/Duration.hpp
      src/Teller/Units/FrameRate.hpp
      src/Teller/Units/Length.hpp
      src/Teller/Units/Literals.hpp
      src/Teller/Window/Window.hpp
)

# impl/は実装と内部用のヘッダー
target_sources(TellerLibrary
  PRIVATE
    impl/Loop/ClockInternal.hpp
    impl/Loop/LoopController.cpp
    impl/Loop/Quit.cpp
    impl/Loop/Timer.cpp
    # 利用者がmain()を書かなかったときだけ取り込まれる
    impl/Main/Main.cpp
    impl/Window/Window.cpp
    impl/Window/WindowInternal.hpp
)
target_include_directories(TellerLibrary PRIVATE impl)

target_compile_features(TellerLibrary PUBLIC cxx_std_20)

target_link_libraries(TellerLibrary
  PRIVATE
    SDL3::SDL3
    teller_project_options
)
