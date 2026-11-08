#pragma once

#include <Teller/Asset/AssetPath.hpp>
#include <Teller/Asset/Image.hpp>

#include <memory>

namespace Teller
{
  namespace Internal
  {
    struct AssetState;

    // constexprのコンストラクタが不完全型の破棄を要求しないようにする
    struct AssetStateDeleter
    {
      void operator()(AssetState* state) const noexcept;
    };
  }

  // 画像などのアセットをまとめて持つ
  // 指定したアセットはこのインスタンスの破棄とともに全て消える
  // 描くにはWindow::subscribe()で購読する
  class Asset
  {
  public:
    // 定数初期化にして、別のcppのグローバル変数の初期化からもimage()を呼べるようにする
    constexpr Asset() noexcept = default;
    virtual ~Asset();

    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;

    // 画像を指定する
    // ファイルはImage::load()か最初のImage::show()で読む
    // 相対パスは実行ファイルの隣、image()を書いたcppの位置の順に探す
    // 同じパスは同じ画像を指す
    virtual Image image(AssetPath path);

    // このAssetの画像へのshow()を確定する
    // 確定しなかったshow()は描かれない
    // Assetをまたいだ重なりは、先に呼んだものが奥になる
    virtual void update();

  private:
    Internal::AssetState& state();

    std::unique_ptr<Internal::AssetState, Internal::AssetStateDeleter> state_;
  };
}
