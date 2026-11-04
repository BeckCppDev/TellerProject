add_library(TellerLibrary STATIC)

# src/は公開ヘッダー
target_sources(TellerLibrary
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS src
    FILES
      src/Teller.hpp
      src/Teller/Units/Length.hpp
      src/Teller/Window/Window.hpp
)

# impl/は実装と内部用のヘッダー
target_sources(TellerLibrary
  PRIVATE
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
