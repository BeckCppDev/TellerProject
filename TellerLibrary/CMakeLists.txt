add_library(TellerLibrary STATIC)

# src/は公開ヘッダー
target_sources(TellerLibrary
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS src
    FILES
      src/Teller.hpp
      src/Teller/Asset/Asset.hpp
      src/Teller/Asset/AssetPath.hpp
      src/Teller/Asset/Font.hpp
      src/Teller/Asset/Image.hpp
      src/Teller/Loop/LoopController.hpp
      src/Teller/Loop/Quit.hpp
      src/Teller/Loop/Timer.hpp
      src/Teller/Random/Random.hpp
      src/Teller/Smooth/Change.hpp
      src/Teller/Smooth/Easing.hpp
      src/Teller/Smooth/Progress.hpp
      src/Teller/Units/Anchor.hpp
      src/Teller/Units/Color.hpp
      src/Teller/Units/Duration.hpp
      src/Teller/Units/FrameRate.hpp
      src/Teller/Units/Length.hpp
      src/Teller/Units/Literals.hpp
      src/Teller/Units/Position.hpp
      src/Teller/Window/Window.hpp
)

# impl/は実装と内部用のヘッダー
target_sources(TellerLibrary
  PRIVATE
    impl/Asset/Asset.cpp
    impl/Asset/AssetFiles.hpp
    impl/Asset/Font.cpp
    impl/Asset/FontData.hpp
    impl/Graphics/DrawBus.cpp
    impl/Graphics/DrawBus.hpp
    impl/Loop/ClockInternal.hpp
    impl/Loop/LoopController.cpp
    impl/Loop/Quit.cpp
    impl/Loop/Timer.cpp
    # 利用者がmain()を書かなかったときだけ取り込まれる
    impl/Main/Main.cpp
    impl/Random/Random.cpp
    impl/Smooth/Change.cpp
    impl/Smooth/Transition.cpp
    impl/Smooth/Transition.hpp
    impl/Text/FontFile.cpp
    impl/Text/FontFile.hpp
    impl/Window/Window.cpp
    impl/Window/WindowInternal.hpp
)
target_include_directories(TellerLibrary PRIVATE impl)

# third_party/はライブラリの中からだけ使い、警告を出さない
target_include_directories(TellerLibrary SYSTEM PRIVATE third_party)

target_compile_features(TellerLibrary PUBLIC cxx_std_20)

target_link_libraries(TellerLibrary
  PRIVATE
    SDL3::SDL3
    teller_project_options
)
