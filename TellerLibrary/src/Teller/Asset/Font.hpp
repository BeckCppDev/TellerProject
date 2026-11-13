#pragma once

#include <Teller/Asset/AssetPath.hpp>
#include <Teller/Smooth/Progress.hpp>
#include <Teller/Units/Anchor.hpp>
#include <Teller/Units/Color.hpp>
#include <Teller/Units/LetterRate.hpp>
#include <Teller/Units/Length.hpp>
#include <Teller/Units/Position.hpp>

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Teller
{
  class Asset;
  class Text;

  namespace Internal
  {
    struct FontData;
    struct TextScript;
    struct TextSource;

    // {}に入れる値
    // 色はタグの中にだけ入れるので文字にしない
    struct TextValue
    {
      // isColorがtrueの間は使わない
      std::string text;

      bool isColor = false;

      // 0xRRGGBB
      std::uint32_t rgb = 0;

      [[nodiscard]] bool operator==(const TextValue&) const = default;
    };

    // 位置やAnchorを値と取り違えないよう型を限る
    // charは文字として描く
    template <typename T>
    concept TextValueType = std::same_as<T, Color> || ((std::integral<T> || std::floating_point<T>) && !std::same_as<T, wchar_t> && !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>) || std::convertible_to<const T&, std::string_view>;

    // 小数はstd::formatの{}と同じ最短の表し方にする
    template <TextValueType T>
    [[nodiscard]] TextValue toTextValue(const T& value)
    {
      TextValue result;
      if constexpr (std::same_as<T, Color>)
      {
        result.isColor = true;
        result.rgb = (static_cast<std::uint32_t>(value.r()) << 16) | (static_cast<std::uint32_t>(value.g()) << 8) | value.b();
      }
      else if constexpr (std::same_as<T, bool>)
      {
        result.text = value ? "true" : "false";
      }
      else if constexpr (std::same_as<T, char>)
      {
        result.text.assign(1, value);
      }
      else if constexpr (std::floating_point<T>)
      {
        // doubleの最短の表し方は24文字以内
        char buffer[64];
        const std::to_chars_result converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        result.text.assign(buffer, converted.ptr);
      }
      else if constexpr (std::integral<T>)
      {
        char buffer[32];
        std::to_chars_result converted;
        if constexpr (std::signed_integral<T>)
        {
          converted = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(value));
        }
        else
        {
          converted = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<unsigned long long>(value));
        }
        result.text.assign(buffer, converted.ptr);
      }
      else
      {
        // nullptrは空の文字列
        if constexpr (std::is_pointer_v<T>)
        {
          if (value == nullptr)
          {
            return result;
          }
        }
        result.text = std::string_view(value);
      }
      return result;
    }

    // Font::show()の文字列
    // std::string_viewで受けると{0, 0}が(ポインタ, 長さ)として入り、位置を省いた呼び出しとして扱われる
    // 推論できない波括弧は受けないようテンプレートにする
    struct ShowText
    {
      template <typename T>
        requires std::convertible_to<const T&, std::string_view>
      ShowText(const T& text)
        : view(text)
      {
      }

      std::string_view view;
    };
  }

  // Asset::font()に渡す設定
  struct FontConfig
  {
    AssetPath path;

    // 1emのピクセル数
    Length size = 16;

    // 1文字ずつ出す速さ
    // 0なら[speed]のタグがない部分を一度に出す
    LetterRate speed = 0;

    // falseなら点の中心が内側かどうかで塗る2値で描く
    bool antialias = false;

    // 以下の6つはフォントの全ての文に効き、タグで外せない

    // 太さを大きさの10分の1だけ足す
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;

    // 0なら縁取りなし
    Length outline = 0;
    Color outlineColor = Color::black;

    // Text::show()とFont::show()で寄せ方を省略したときの寄せ方
    Anchor anchor = Anchor::topLeft;
  };

  // Font::text()に渡す設定
  // 指定しなかった項目はフォントの設定を使う
  struct TextConfig
  {
    std::optional<Length> size;
  };

  // Font::text()が返す文
  // 進み具合は文ごとに持ち、コピーとは共有しない
  class Text
  {
  public:
    Text() = default;

    // フォントの寄せ方で置く
    // フォントを読み込んでいなければ初回だけ読む
    // Asset::update()で確定するまで描かれず、1周期だけ有効
    // 文字送りはshow()を呼んだ周期だけ1周期分進む
    // 戻り値は呼んだ後の進み具合
    Progress show(Position position = Position::central);

    // 文全体を囲む箱のanchorの点を指定した位置に合わせる
    // 各行の横の揃え方もanchorに従う
    // 箱はまだ出していない文字も含めて決める
    Progress show(Position position, Anchor anchor);

    // 以下の3つは位置と寄せ方の後ろに{}の値を並べる(msg.show({0, 0}, hp, maxHp))
    // 値が変わっても進み具合は続き、各文字が出る時刻は新しい文字列で決め直す
    // {}と値の数が合わないとデバッグビルドで1度だけ警告し、値のない{}はそのまま描く
    template <Internal::TextValueType... Values>
      requires(sizeof...(Values) > 0)
    Progress show(Position position, Anchor anchor, const Values&... values)
    {
      return showWithValues(position, anchor, { Internal::toTextValue(values)... });
    }

    template <Internal::TextValueType... Values>
      requires(sizeof...(Values) > 0)
    Progress show(Position position, const Values&... values)
    {
      return showWithValues(position, anchor_, { Internal::toTextValue(values)... });
    }

    // 位置を省略すると画面の中央
    template <Internal::TextValueType... Values>
      requires(sizeof...(Values) > 0)
    Progress show(const Values&... values)
    {
      return showWithValues(Position::central, anchor_, { Internal::toTextValue(values)... });
    }

    // 最後のshow()の時点の値で、呼ぶ前はstarted
    // 一度に全部出す文は最初のshow()からfinished
    [[nodiscard]] Progress getProgress() const;

    // finishedで残りを一気に出し、startedで最初から出し直す
    // runningは何もしない
    void setProgress(Progress progress);

    [[nodiscard]] bool isFinished() const;

  private:
    friend class Font;

    Text(const std::weak_ptr<Internal::FontData>& data, std::shared_ptr<const Internal::TextSource> source, int size, bool antialias, Anchor anchor);

    Progress showWithValues(Position position, Anchor anchor, std::vector<Internal::TextValue> values);

    // 出す文字の数を返す
    std::size_t advance();

    std::weak_ptr<Internal::FontData> data_;

    // text()に渡した文字列とフォントの設定
    // 変わらないのでコピーと共有する
    std::shared_ptr<const Internal::TextSource> source_;

    // 値が変わると作り直すのでコピーと共有してよい
    std::shared_ptr<const Internal::TextScript> script_;

    // script_を作ったときの値
    std::vector<Internal::TextValue> values_;

    // {}の数の警告は最初のshow()で読み直すときに出す
    bool parsedForShow_ = false;

    int size_ = 16;
    bool antialias_ = false;
    Anchor anchor_ = Anchor::topLeft;

    Progress progress_ = Progress::started;

    bool shown_ = false;

    // setProgress(Progress::finished)で一気に出したか
    bool skipped_ = false;

    // 最後にshow()を呼んだ周期のtimer().frames()
    std::uint64_t lastFrame_ = 0;

    // show()を呼んだ周期の数で、最初の周期は0
    std::uint64_t shownFrames_ = 0;
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
    // [speed=15_lps]は以降の速さを変え、[/speed]で1つ前に戻す
    // [wait=1_sec]は次の文字の前で待つ
    // 単位はsec、ms、framesで、省略すると秒
    // [bold](略称[b])、[italic]([i])、[underline]([u])、[strikethrough]([st])は閉じるまで見た目を変える
    // [outline=2_px]([o2])は閉じるまで縁取りし、値を省略すると1px
    // [color=red]([red]、[c=red])は[/color]か[/c]で閉じるまで文字の色を変え、色はColorの定数の名前か#FF0000
    // [color=green,outline=2_px]は縁の色と太さを変えて[/outline]で閉じ、並びは自由
    // 略称では色の項目名を省いて[o2,green]と書ける
    // 色にrainbowと書くと2秒で色合いが一周する([rainbow]、[o2,rainbow])
    // 閉じタグは正式名称と略称のどちらでもよく、[b]を[/bold]で閉じられる
    // [speed]は[s15]、[wait]は[w0.5]と略せる
    // [[と]]は角括弧そのもの
    // {}にはText::show()に渡した値が出てきた順に入る
    // 値の中の[と]はタグとして読まない
    // タグの中の{}にはColorだけを入れられる([o2,{}]、[{}])
    // {{と}}は波括弧そのもの
    [[nodiscard]] Text text(std::string_view text, TextConfig config = {}) const;

    // text()の文を作らずに描く
    // 進み具合はこのFontの変数が文字列ごとに持ち、{}に値を入れる前の文字列が同じなら続きから出す
    // 前の周期に渡さなかった文字列は進み具合を忘れ、次は1文字目から出す
    // 1周期に同じ文字列を何回渡しても進むのは1周期分
    // コピーしたFontは進み具合を写し、以降は別々に進む
    // LoopControllerがないときは進み具合を覚えず、一度に全部出す
    // 位置を省略すると画面の中央、寄せ方を省略するとフォントの寄せ方
    // タグと{}の扱い、戻り値はtext()とText::show()と同じ
    template <Internal::TextValueType... Values>
    Progress show(Internal::ShowText text, const Values&... values)
    {
      return showDirect(Position::central, anchor_, text.view, { Internal::toTextValue(values)... });
    }

    template <Internal::TextValueType... Values>
    Progress show(Position position, Internal::ShowText text, const Values&... values)
    {
      return showDirect(position, anchor_, text.view, { Internal::toTextValue(values)... });
    }

    template <Internal::TextValueType... Values>
    Progress show(Position position, Anchor anchor, Internal::ShowText text, const Values&... values)
    {
      return showDirect(position, anchor, text.view, { Internal::toTextValue(values)... });
    }

  private:
    friend class Asset;

    Font(const std::shared_ptr<Internal::FontData>& data, const FontConfig& config);

    // show()に渡された文字列の進み具合
    struct ShownText
    {
      std::string markup;
      Text text;

      // 最後に渡された周期のtimer().frames()
      std::uint64_t lastFrame = 0;
    };

    Progress showDirect(Position position, Anchor anchor, std::string_view markup, std::vector<Internal::TextValue> values);

    // Assetの破棄を検出するため、延命しない弱い参照で持つ
    std::weak_ptr<Internal::FontData> data_;
    int size_ = 16;
    double speed_ = 0.0;
    bool antialias_ = false;
    bool bold_ = false;
    bool italic_ = false;
    bool underline_ = false;
    bool strikethrough_ = false;
    int outline_ = 0;
    std::uint32_t outlineColor_ = 0;
    Anchor anchor_ = Anchor::topLeft;

    // 1周期に渡す文字列は数個なので順に探す
    std::vector<ShownText> shownTexts_;
  };
}
