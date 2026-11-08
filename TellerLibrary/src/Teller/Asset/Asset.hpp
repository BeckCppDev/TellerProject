#pragma once

#include <Teller/Asset/Image.hpp>

#include <memory>
#include <source_location>
#include <string_view>

namespace Teller
{
  namespace Internal
  {
    struct AssetState;
  }

  // 画像などのアセットをまとめて持つ
  // 読み込んだアセットはこのインスタンスの破棄とともに全て消える
  // 描くにはWindow::subscribe()で購読する
  class Asset
  {
  public:
    // locationは相対パスの基準になるので通常は省略する
    // std::make_unique<Asset>()では標準ライブラリの場所になるため、std::source_location::current()を渡す
    explicit Asset(std::source_location location = std::source_location::current());
    virtual ~Asset();

    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;

    // PNGを読み込む
    // 相対パスは実行ファイルの隣、このインスタンスを宣言したcppの位置の順に探す
    // 同じパスは1回目に読み込んだものを返す
    // 失敗したときはログを出し、何も描かないImageを返す
    virtual Image loadImage(std::string_view path);

    // このAssetの画像へのshow()を確定する
    // 確定しなかったshow()は描かれない
    // Assetをまたいだ重なりは、先に呼んだものが奥になる
    virtual void update();

  private:
    std::unique_ptr<Internal::AssetState> state_;
  };
}
