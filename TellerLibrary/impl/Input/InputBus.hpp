#pragma once

#include <SDL3/SDL.h>

namespace Teller
{
  class Window;
}

// OSのキー入力は全てreceiveKey()とreleaseWindowKeys()を通す
// TASではここで記録した入力に差し替える
// 入力はそのウインドウを購読している全てのInputに渡り、各Inputのupdate()で確定する
namespace Teller::Internal
{
  // キーリピートは渡さない
  void receiveKey(const Window* window, SDL_Scancode scancode, bool down);

  // フォーカスを失ったときと閉じたときに呼ぶ
  void releaseWindowKeys(const Window* window);

  // Windowが消える前に、全てのInputからそのウインドウの購読を外す
  void releaseInputSubscriptions(const Window* window);
}
