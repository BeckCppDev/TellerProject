#pragma once

#include <Teller/Input/ButtonState.hpp>

#include <memory>

namespace Teller
{
  class Window;

  namespace Internal
  {
    struct InputState;
  }

  // 購読したWindowで押されたキーを読む
  // キーはウインドウにフォーカスがある間だけ届き、フォーカスを失ったときに押されていたキーは離したものとして扱う
  // キーリピートは数えない
  class Input
  {
  public:
    Input();
    virtual ~Input();

    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Window::create()の前に呼んでもよい
    // 重ねて購読しても1回分として扱う
    // Window::destroy()の後も購読は続く
    virtual void subscribe(const Window& window);

    // そのウインドウで押されていたキーは次のupdate()で離したものとして扱う
    virtual void unsubscribe(const Window& window);

    // 前のupdate()から後に届いた入力を取り込む
    // 呼ばなかった周期の入力は次に呼んだときにまとめて取り込む
    virtual void update();

    // キーは文字ではなく位置で読み、US配列のキーボードでその名前が書かれた位置を指す

    [[nodiscard]] virtual ButtonState getA() const;
    [[nodiscard]] virtual ButtonState getB() const;
    [[nodiscard]] virtual ButtonState getC() const;
    [[nodiscard]] virtual ButtonState getD() const;
    [[nodiscard]] virtual ButtonState getE() const;
    [[nodiscard]] virtual ButtonState getF() const;
    [[nodiscard]] virtual ButtonState getG() const;
    [[nodiscard]] virtual ButtonState getH() const;
    [[nodiscard]] virtual ButtonState getI() const;
    [[nodiscard]] virtual ButtonState getJ() const;
    [[nodiscard]] virtual ButtonState getK() const;
    [[nodiscard]] virtual ButtonState getL() const;
    [[nodiscard]] virtual ButtonState getM() const;
    [[nodiscard]] virtual ButtonState getN() const;
    [[nodiscard]] virtual ButtonState getO() const;
    [[nodiscard]] virtual ButtonState getP() const;
    [[nodiscard]] virtual ButtonState getQ() const;
    [[nodiscard]] virtual ButtonState getR() const;
    [[nodiscard]] virtual ButtonState getS() const;
    [[nodiscard]] virtual ButtonState getT() const;
    [[nodiscard]] virtual ButtonState getU() const;
    [[nodiscard]] virtual ButtonState getV() const;
    [[nodiscard]] virtual ButtonState getW() const;
    [[nodiscard]] virtual ButtonState getX() const;
    [[nodiscard]] virtual ButtonState getY() const;
    [[nodiscard]] virtual ButtonState getZ() const;

    // テンキーは含まない
    [[nodiscard]] virtual ButtonState get0() const;
    [[nodiscard]] virtual ButtonState get1() const;
    [[nodiscard]] virtual ButtonState get2() const;
    [[nodiscard]] virtual ButtonState get3() const;
    [[nodiscard]] virtual ButtonState get4() const;
    [[nodiscard]] virtual ButtonState get5() const;
    [[nodiscard]] virtual ButtonState get6() const;
    [[nodiscard]] virtual ButtonState get7() const;
    [[nodiscard]] virtual ButtonState get8() const;
    [[nodiscard]] virtual ButtonState get9() const;

    // F4はWindowの全画面の切り替えにも使われる
    [[nodiscard]] virtual ButtonState getF1() const;
    [[nodiscard]] virtual ButtonState getF2() const;
    [[nodiscard]] virtual ButtonState getF3() const;
    [[nodiscard]] virtual ButtonState getF4() const;
    [[nodiscard]] virtual ButtonState getF5() const;
    [[nodiscard]] virtual ButtonState getF6() const;
    [[nodiscard]] virtual ButtonState getF7() const;
    [[nodiscard]] virtual ButtonState getF8() const;
    [[nodiscard]] virtual ButtonState getF9() const;
    [[nodiscard]] virtual ButtonState getF10() const;
    [[nodiscard]] virtual ButtonState getF11() const;
    [[nodiscard]] virtual ButtonState getF12() const;

    [[nodiscard]] virtual ButtonState getUp() const;
    [[nodiscard]] virtual ButtonState getDown() const;
    [[nodiscard]] virtual ButtonState getLeft() const;
    [[nodiscard]] virtual ButtonState getRight() const;

    // テンキーのEnterは含まない
    [[nodiscard]] virtual ButtonState getEnter() const;
    [[nodiscard]] virtual ButtonState getEscape() const;
    [[nodiscard]] virtual ButtonState getSpace() const;
    [[nodiscard]] virtual ButtonState getTab() const;
    [[nodiscard]] virtual ButtonState getBackspace() const;

    // 左右は別のキーとして読む
    [[nodiscard]] virtual ButtonState getLeftShift() const;
    [[nodiscard]] virtual ButtonState getRightShift() const;
    [[nodiscard]] virtual ButtonState getLeftControl() const;
    [[nodiscard]] virtual ButtonState getRightControl() const;
    [[nodiscard]] virtual ButtonState getLeftAlt() const;
    [[nodiscard]] virtual ButtonState getRightAlt() const;

    [[nodiscard]] virtual ButtonState getInsert() const;
    [[nodiscard]] virtual ButtonState getDelete() const;
    [[nodiscard]] virtual ButtonState getHome() const;
    [[nodiscard]] virtual ButtonState getEnd() const;
    [[nodiscard]] virtual ButtonState getPageUp() const;
    [[nodiscard]] virtual ButtonState getPageDown() const;

  private:
    std::unique_ptr<Internal::InputState> state_;
  };
}
