#pragma once

namespace Teller
{
  // 値はInput::update()で切り替わり、次のupdate()まで変わらない
  struct ButtonState
  {
    // 押された直後の1周期だけtrue
    bool click = false;

    bool press = false;

    // 離された直後の1周期だけtrue
    // 1周期の間に押して離したときはclickと一緒にtrueになる
    bool release = false;

    // 最初はfalseで、clickのたびに反転する
    bool toggle = false;
  };
}
