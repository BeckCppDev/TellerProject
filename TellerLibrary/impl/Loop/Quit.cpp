#include <Teller/Loop/Quit.hpp>

#include <cstdlib>

namespace Teller
{
  void quit()
  {
    // SDL3の後片付けはWindowのデストラクタが行う
    // 先にSDL_Quit()を呼ぶとデストラクタが片付け済みのSDL3に触れる
    std::exit(0);
  }
}
