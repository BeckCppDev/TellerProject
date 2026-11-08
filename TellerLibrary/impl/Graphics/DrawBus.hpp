#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <span>

// 描く内容の通り道
// 送り主はWindowを知らずに流し、Windowは購読している送り主の内容だけを描く
// show()の指示は送り主のupdate()で確定し、確定した順に奥から手前へ重なる
namespace Teller::Internal
{
  // メモリ上の画像
  // GPU上の画像はウインドウごとに要るのでWindowが持つ
  struct ImageData
  {
    ImageData(const void* source, SDL_Surface* surface);
    ~ImageData();

    ImageData(const ImageData&) = delete;
    ImageData& operator=(const ImageData&) = delete;

    // 送り主の識別にだけ使う
    const void* source;
    SDL_Surface* surface;
  };

  // 送り主が破棄されても周期の終わりまで画像を保つ
  struct DrawMessage
  {
    std::shared_ptr<const ImageData> image;
    double x;
    double y;
  };

  class DrawListener
  {
  public:
    // 確定した内容を奥から順に受け取る
    virtual void onFrame(std::span<const DrawMessage> messages) = 0;

    // 画像が消える直前に呼ばれる
    virtual void onImageReleased(const ImageData* image) = 0;

    // 送り主が破棄されるときに呼ばれる
    virtual void onSourceReleased(const void* source) = 0;

  protected:
    ~DrawListener() = default;
  };

  void addDrawListener(DrawListener& listener);
  void removeDrawListener(DrawListener& listener);

  void publishDraw(DrawMessage message);

  // sourceの指示を確定した内容の末尾へ移す
  // 同じ送り主の中ではshow()の順を保つ
  void commitDraw(const void* source);

  // 確定した内容を受け取り手に渡し、確定しなかった指示とともに捨てる
  // LoopController::update()と、それが呼ばれていない間のisOpen()と同梱のmain()が呼ぶ
  void deliverDraw();

  // 購読忘れの警告にだけ使う
  void noteSubscribed(const void* source);

  void notifySourceReleased(const void* source);
}
