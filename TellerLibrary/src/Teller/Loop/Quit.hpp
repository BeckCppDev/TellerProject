#pragma once

namespace Teller
{
  // 後片付けをして終了し、戻らない
  // 静的記憶域のオブジェクトは破棄されるが、main()の局所変数は破棄されない
  [[noreturn]] void quit();
}
