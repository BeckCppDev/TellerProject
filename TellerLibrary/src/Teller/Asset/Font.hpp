#pragma once

#include <Teller/Asset/AssetPath.hpp>
#include <Teller/Units/Anchor.hpp>
#include <Teller/Units/Length.hpp>
#include <Teller/Units/Position.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Teller
{
  class Asset;
  class Text;

  namespace Internal
  {
    struct FontData;
  }

  // Asset::font()に渡す設定
  struct FontConfig
  {
    AssetPath path;

    // 1emのピクセル数
    Length size = 16;

    // falseなら点の中心が内側かどうかで塗る2値で描く
    bool antialias = false;

    // Text::show()で寄せ方を省略したときの寄せ方
    Anchor anchor = Anchor::topLeft;
  };

  // Font::text()に渡す設定
  // 指定しなかった項目はフォントの設定を使う
  struct TextConfig
  {
    std::optional<Length> size;
  };

  // Asset::font()が返すフォント
  // ファイルはAssetが持ち、コピーしても同じファイルを指す
  // 大きさなどの設定はFontごとに持つ
  // 何も指定していないFontとAssetの破棄後は、作った文が何も描かない
  class Font
  {
  public:
    Font() = default;

    // 読み込み済みなら何もしない
    // 失敗したときはログを出してfalseを返し、次の呼び出しで読み直す
    bool load() const;

    // 文字列はUTF-8として読み、\nで改行する
    [[nodiscard]] Text text(std::string_view text, TextConfig config = {}) const;

  private:
    friend class Asset;

    Font(const std::shared_ptr<Internal::FontData>& data, const FontConfig& config);

    // Assetの破棄を検出するため、延命しない弱い参照で持つ
    std::weak_ptr<Internal::FontData> data_;
    int size_ = 16;
    bool antialias_ = false;
    Anchor anchor_ = Anchor::topLeft;
  };

  // Font::text()が返す文
  class Text
  {
  public:
    Text() = default;

    // フォントの寄せ方で置く
    // フォントを読み込んでいなければ初回だけ読む
    // Asset::update()で確定するまで描かれず、1周期だけ有効
    void show(Position position = Position::central) const;

    // 文全体を囲む箱のanchorの点を指定した位置に合わせる
    // 各行の横の揃え方もanchorに従う
    void show(Position position, Anchor anchor) const;

  private:
    friend class Font;

    Text(const std::weak_ptr<Internal::FontData>& data, std::string_view text, int size, bool antialias, Anchor anchor);

    std::weak_ptr<Internal::FontData> data_;
    std::string text_;
    int size_ = 16;
    bool antialias_ = false;
    Anchor anchor_ = Anchor::topLeft;
  };
}
