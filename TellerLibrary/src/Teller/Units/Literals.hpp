// 単位のリテラルを使えるようにする
// 無効にするときは、読み込む前に#define USE_TELLER_LITERALS falseを書く
#pragma once

#ifndef USE_TELLER_LITERALS
  #define USE_TELLER_LITERALS true
#endif

// 中身は各単位のヘッダーが足す
namespace Teller::literals
{
}

#if USE_TELLER_LITERALS
using namespace Teller::literals;
#endif
