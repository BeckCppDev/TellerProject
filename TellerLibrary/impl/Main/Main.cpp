#include <Loop/ClockInternal.hpp>
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

    // LoopControllerがないときは代わりにOSからの通知を処理する
    if (!Teller::Internal::isClockActive())
    {
      Teller::Internal::processEvents();
    }
  }
}
