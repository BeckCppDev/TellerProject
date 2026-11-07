#include <Window/WindowInternal.hpp>

void setup();
void loop();

int main()
{
  setup();
  // 終了は原則Teller::quit()で行うこと
  for (;;)
  {
    loop();

    // update()が呼ばれていないときは代わりに通知を処理する
    if (!Teller::Internal::areEventsHandledByLoop())
    {
      Teller::Internal::processEvents();
    }
  }
}
