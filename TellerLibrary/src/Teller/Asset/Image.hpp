#pragma once

#include <Teller/Units/Position.hpp>

#include <memory>

namespace Teller
{
  class Asset;

  namespace Internal
  {
    struct ImageData;
  }

  // Asset::loadImage()が返す画像
  // 実体はAssetが持ち、コピーしても同じ画像を指す
  // 読み込む前とAssetの破棄後はshow()が何も描かない
  class Image
  {
  public:
    Image() = default;

    // 画像の中心を指定した位置に置く
    // Asset::update()で確定するまで描かれず、1周期だけ有効
    // 同じAssetの中では後に呼んだものが手前になる
    void show(Position position = Position::central) const;

  private:
    friend class Asset;

    explicit Image(const std::shared_ptr<Internal::ImageData>& data);

    // Assetの破棄を検出するため、延命しない弱い参照で持つ
    std::weak_ptr<Internal::ImageData> data_;
  };
}
