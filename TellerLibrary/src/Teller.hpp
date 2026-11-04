// TellerLibraryの全機能を読み込む
#pragma once

#include <Teller/Units/Duration.hpp>
#include <Teller/Units/Length.hpp>
#include <Teller/Window/Window.hpp>

// 単位付きの書き方を使えるようにする
// 無効にするときは、読み込む前に#define USE_TELLER_LITERALS falseを書く
#ifndef USE_TELLER_LITERALS
  #define USE_TELLER_LITERALS true
#endif

#if USE_TELLER_LITERALS
using namespace Teller::literals;
#endif
