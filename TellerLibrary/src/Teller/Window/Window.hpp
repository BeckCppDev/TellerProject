#pragma once

#include <Teller/Smooth/Change.hpp>
#include <Teller/Units/FrameRate.hpp>
#include <Teller/Units/Length.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Teller
{
  enum class FullscreenMode
  {
    windowed,
    // 解像度を変えずに画面いっぱいに広げる
    borderless,
    // 解像度をウインドウのサイズに合わせて切り替える
    exclusive,
  };

  // 指定する項目は宣言の順番どおりに並べる
  struct WindowConfig
  {
    std::string title = "Teller";

    Length width = 640;
    Length height = 480;

    // 省略した軸は画面の中央に置く
    std::optional<Length> x;
    std::optional<Length> y;

    // xとyより優先する
    bool centered = false;

    // 省略すると制限しない
    std::optional<Length> minWidth;
    std::optional<Length> minHeight;
    std::optional<Length> maxWidth;
    std::optional<Length> maxHeight;

    // 幅÷高さ
    // 0のときは固定しない
    float aspectRatio = 0.0f;

    bool resizable = false;

    // タイトルバーと枠を表示する
    bool decorated = true;

    FullscreenMode fullscreen = FullscreenMode::windowed;

    // F4キーでの切り替えを許可する
    bool allowFullscreenToggle = true;

    bool alwaysOnTop = false;

    // プレイヤーが閉じたときにTeller::quit()を呼ぶ
    // destroy()では呼ばない
    bool quitOnClose = true;

    // .smooth()の変化を1秒あたりに進める回数
    // 省略するとLoopControllerのlogicと同じになる
    std::optional<FrameRate> smoothRate;
  };

  namespace Internal
  {
    struct WindowState;
  }

  // create()を呼ぶまで画面に出ない
  class Window
  {
  public:
    explicit Window(const WindowConfig& config = {});
    virtual ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // 既に出ているときは何もしない
    // 失敗したときはisOpen()がfalseのままになる
    virtual void create();

    // 出ていないときは何もしない
    // 閉じた後もcreate()で再び出せる
    virtual void destroy();

    // LoopControllerがないときはOSからの通知もここで処理する
    // TASで再現できないので推奨しない
    [[nodiscard]] virtual bool isOpen() const;

    [[nodiscard]] virtual std::string getTitle() const;
    virtual void setTitle(std::string_view title);

    // create()の前は設定値を、後は実際の値を返す
    [[nodiscard]] virtual int getWidth() const;
    [[nodiscard]] virtual int getHeight() const;

    // create()の前は.smooth()を付けても即座に反映する
    virtual Change setWidth(Length width);
    virtual Change setHeight(Length height);

    // create()の前は設定値を、後は実際の値を返す
    // create()の前で指定していない軸は0を返す
    [[nodiscard]] virtual int getX() const;
    [[nodiscard]] virtual int getY() const;

    // create()の前は.smooth()を付けても即座に反映する
    virtual Change setX(Length x);
    virtual Change setY(Length y);

    // 指定していないときはlogicの値を返す
    [[nodiscard]] virtual FrameRate getSmoothRate() const;

    // 次に始める変化から使う
    virtual void setSmoothRate(FrameRate rate);

    // logicと同じに戻す
    virtual void resetSmoothRate();

  private:
    std::unique_ptr<Internal::WindowState> state_;
  };
}
