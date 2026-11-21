#include <Teller/Input/Input.hpp>

#include <Input/InputBus.hpp>

#include <algorithm>
#include <array>
#include <vector>

// キーの入力は2段階で扱う
// 1. 届いた通知を、購読しているInputのKeyRecordに書き留める
// 2. Input::update()で、書き留めた分からButtonStateを作る
namespace Teller::Internal
{
  namespace
  {
    // 前のupdate()から後の入力
    struct KeyRecord
    {
      // 離されている間はnullptr
      const Window* heldBy = nullptr;

      bool clicked = false;
      bool released = false;

      // clickの回数が奇数ならtrue
      bool toggleFlipped = false;
    };
  }

  struct InputState
  {
    InputState();
    ~InputState();

    InputState(const InputState&) = delete;
    InputState& operator=(const InputState&) = delete;

    [[nodiscard]] bool isSubscribed(const Window* window) const;

    void press(const Window* window, SDL_Scancode scancode);
    void release(SDL_Scancode scancode);
    void releaseAll(const Window* window);
    void commit();

    [[nodiscard]] ButtonState read(SDL_Scancode scancode) const;

    std::vector<const Window*> subscriptions;

    // スキャンコードで引く
    std::array<KeyRecord, SDL_SCANCODE_COUNT> records{};
    std::array<ButtonState, SDL_SCANCODE_COUNT> states{};

#ifndef NDEBUG
    // update()の呼び忘れの警告に使う
    bool everCommitted = false;
    bool everReceived = false;
#endif
  };

  namespace
  {
    // グローバルのInputとWindowのデストラクタから呼ばれるので、破棄順に左右されないよう解放しない
    std::vector<InputState*>& inputs()
    {
      static auto* instance = new std::vector<InputState*>;
      return *instance;
    }
  }

  InputState::InputState()
  {
    inputs().push_back(this);
  }

  InputState::~InputState()
  {
    std::erase(inputs(), this);
  }

  bool InputState::isSubscribed(const Window* window) const
  {
    return std::ranges::find(subscriptions, window) != subscriptions.end();
  }

  void InputState::press(const Window* window, SDL_Scancode scancode)
  {
    KeyRecord& record = records[scancode];
    // 押したまま別のウインドウから届いた通知は押し直しとして数えない
    if (record.heldBy != nullptr)
    {
      return;
    }
    record.heldBy = window;
    record.clicked = true;
    record.toggleFlipped = !record.toggleFlipped;
#ifndef NDEBUG
    everReceived = true;
#endif
  }

  void InputState::release(SDL_Scancode scancode)
  {
    KeyRecord& record = records[scancode];
    if (record.heldBy == nullptr)
    {
      return;
    }
    record.heldBy = nullptr;
    record.released = true;
  }

  void InputState::releaseAll(const Window* window)
  {
    for (KeyRecord& record : records)
    {
      if (record.heldBy == window)
      {
        record.heldBy = nullptr;
        record.released = true;
      }
    }
  }

  void InputState::commit()
  {
    for (std::size_t index = 0; index < records.size(); ++index)
    {
      KeyRecord& record = records[index];
      ButtonState& state = states[index];
      state.click = record.clicked;
      state.press = record.heldBy != nullptr;
      state.release = record.released;
      if (record.toggleFlipped)
      {
        state.toggle = !state.toggle;
      }
      record.clicked = false;
      record.released = false;
      record.toggleFlipped = false;
    }
#ifndef NDEBUG
    everCommitted = true;
#endif
  }

  ButtonState InputState::read(SDL_Scancode scancode) const
  {
#ifndef NDEBUG
    static bool warned = false;
    if (!warned && everReceived && !everCommitted)
    {
      warned = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Input: keys are not read until update() is called");
    }
#endif
    return states[scancode];
  }

  void receiveKey(const Window* window, SDL_Scancode scancode, bool down)
  {
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
    {
      return;
    }
    for (InputState* input : inputs())
    {
      if (!input->isSubscribed(window))
      {
        continue;
      }
      if (down)
      {
        input->press(window, scancode);
      }
      else
      {
        input->release(scancode);
      }
    }
  }

  void releaseWindowKeys(const Window* window)
  {
    for (InputState* input : inputs())
    {
      input->releaseAll(window);
    }
  }

  void releaseInputSubscriptions(const Window* window)
  {
    for (InputState* input : inputs())
    {
      input->releaseAll(window);
      std::erase(input->subscriptions, window);
    }
  }
}

namespace Teller
{
  Input::Input()
    : state_(std::make_unique<Internal::InputState>())
  {
  }

  Input::~Input() = default;

  void Input::subscribe(const Window& window)
  {
    if (!state_->isSubscribed(&window))
    {
      state_->subscriptions.push_back(&window);
    }
  }

  void Input::unsubscribe(const Window& window)
  {
    state_->releaseAll(&window);
    std::erase(state_->subscriptions, &window);
  }

  void Input::update()
  {
#ifndef NDEBUG
    static bool warned = false;
    if (!warned && state_->subscriptions.empty())
    {
      warned = true;
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Input: update() has no effect because no Window has been subscribed");
    }
#endif
    state_->commit();
  }

#define TELLER_INPUT_KEY(name, scancode) \
  ButtonState Input::get##name() const \
  { \
    return state_->read(scancode); \
  }

  TELLER_INPUT_KEY(A, SDL_SCANCODE_A)
  TELLER_INPUT_KEY(B, SDL_SCANCODE_B)
  TELLER_INPUT_KEY(C, SDL_SCANCODE_C)
  TELLER_INPUT_KEY(D, SDL_SCANCODE_D)
  TELLER_INPUT_KEY(E, SDL_SCANCODE_E)
  TELLER_INPUT_KEY(F, SDL_SCANCODE_F)
  TELLER_INPUT_KEY(G, SDL_SCANCODE_G)
  TELLER_INPUT_KEY(H, SDL_SCANCODE_H)
  TELLER_INPUT_KEY(I, SDL_SCANCODE_I)
  TELLER_INPUT_KEY(J, SDL_SCANCODE_J)
  TELLER_INPUT_KEY(K, SDL_SCANCODE_K)
  TELLER_INPUT_KEY(L, SDL_SCANCODE_L)
  TELLER_INPUT_KEY(M, SDL_SCANCODE_M)
  TELLER_INPUT_KEY(N, SDL_SCANCODE_N)
  TELLER_INPUT_KEY(O, SDL_SCANCODE_O)
  TELLER_INPUT_KEY(P, SDL_SCANCODE_P)
  TELLER_INPUT_KEY(Q, SDL_SCANCODE_Q)
  TELLER_INPUT_KEY(R, SDL_SCANCODE_R)
  TELLER_INPUT_KEY(S, SDL_SCANCODE_S)
  TELLER_INPUT_KEY(T, SDL_SCANCODE_T)
  TELLER_INPUT_KEY(U, SDL_SCANCODE_U)
  TELLER_INPUT_KEY(V, SDL_SCANCODE_V)
  TELLER_INPUT_KEY(W, SDL_SCANCODE_W)
  TELLER_INPUT_KEY(X, SDL_SCANCODE_X)
  TELLER_INPUT_KEY(Y, SDL_SCANCODE_Y)
  TELLER_INPUT_KEY(Z, SDL_SCANCODE_Z)

  TELLER_INPUT_KEY(0, SDL_SCANCODE_0)
  TELLER_INPUT_KEY(1, SDL_SCANCODE_1)
  TELLER_INPUT_KEY(2, SDL_SCANCODE_2)
  TELLER_INPUT_KEY(3, SDL_SCANCODE_3)
  TELLER_INPUT_KEY(4, SDL_SCANCODE_4)
  TELLER_INPUT_KEY(5, SDL_SCANCODE_5)
  TELLER_INPUT_KEY(6, SDL_SCANCODE_6)
  TELLER_INPUT_KEY(7, SDL_SCANCODE_7)
  TELLER_INPUT_KEY(8, SDL_SCANCODE_8)
  TELLER_INPUT_KEY(9, SDL_SCANCODE_9)

  TELLER_INPUT_KEY(F1, SDL_SCANCODE_F1)
  TELLER_INPUT_KEY(F2, SDL_SCANCODE_F2)
  TELLER_INPUT_KEY(F3, SDL_SCANCODE_F3)
  TELLER_INPUT_KEY(F4, SDL_SCANCODE_F4)
  TELLER_INPUT_KEY(F5, SDL_SCANCODE_F5)
  TELLER_INPUT_KEY(F6, SDL_SCANCODE_F6)
  TELLER_INPUT_KEY(F7, SDL_SCANCODE_F7)
  TELLER_INPUT_KEY(F8, SDL_SCANCODE_F8)
  TELLER_INPUT_KEY(F9, SDL_SCANCODE_F9)
  TELLER_INPUT_KEY(F10, SDL_SCANCODE_F10)
  TELLER_INPUT_KEY(F11, SDL_SCANCODE_F11)
  TELLER_INPUT_KEY(F12, SDL_SCANCODE_F12)

  TELLER_INPUT_KEY(Up, SDL_SCANCODE_UP)
  TELLER_INPUT_KEY(Down, SDL_SCANCODE_DOWN)
  TELLER_INPUT_KEY(Left, SDL_SCANCODE_LEFT)
  TELLER_INPUT_KEY(Right, SDL_SCANCODE_RIGHT)

  TELLER_INPUT_KEY(Enter, SDL_SCANCODE_RETURN)
  TELLER_INPUT_KEY(Escape, SDL_SCANCODE_ESCAPE)
  TELLER_INPUT_KEY(Space, SDL_SCANCODE_SPACE)
  TELLER_INPUT_KEY(Tab, SDL_SCANCODE_TAB)
  TELLER_INPUT_KEY(Backspace, SDL_SCANCODE_BACKSPACE)

  TELLER_INPUT_KEY(LeftShift, SDL_SCANCODE_LSHIFT)
  TELLER_INPUT_KEY(RightShift, SDL_SCANCODE_RSHIFT)
  TELLER_INPUT_KEY(LeftControl, SDL_SCANCODE_LCTRL)
  TELLER_INPUT_KEY(RightControl, SDL_SCANCODE_RCTRL)
  TELLER_INPUT_KEY(LeftAlt, SDL_SCANCODE_LALT)
  TELLER_INPUT_KEY(RightAlt, SDL_SCANCODE_RALT)

  TELLER_INPUT_KEY(Insert, SDL_SCANCODE_INSERT)
  TELLER_INPUT_KEY(Delete, SDL_SCANCODE_DELETE)
  TELLER_INPUT_KEY(Home, SDL_SCANCODE_HOME)
  TELLER_INPUT_KEY(End, SDL_SCANCODE_END)
  TELLER_INPUT_KEY(PageUp, SDL_SCANCODE_PAGEUP)
  TELLER_INPUT_KEY(PageDown, SDL_SCANCODE_PAGEDOWN)

#undef TELLER_INPUT_KEY
}
