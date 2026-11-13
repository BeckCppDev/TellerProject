#pragma once

#include <Teller/Asset/AssetPath.hpp>
#include <Teller/Smooth/Progress.hpp>
#include <Teller/Units/Anchor.hpp>
#include <Teller/Units/Color.hpp>
#include <Teller/Units/LetterRate.hpp>
#include <Teller/Units/Length.hpp>
#include <Teller/Units/Position.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace Teller
{
  class Asset;
  class Text;

  namespace Internal
  {
    struct FontData;
    struct TextScript;
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
    // [speed=15_lps]は以降の速さを変え、[/speed]で1つ前に戻す
    // [wait=1_sec]は次の文字の前で待つ
    // 単位はsec、ms、framesで、省略すると秒
    // [bold](略称[b])、[italic]([i])、[underline]([u])、[strikethrough]([st])は閉じるまで見た目を変える
    // [outline=2_px]([o2])は閉じるまで縁取りし、値を省略すると1px
    // [color=red]([c=red]、[cred])は閉じるまで文字の色を変え、色はColorの定数の名前か#FF0000
    // [color=green,outline=2_px]は縁の色と太さを変えて[/outline]で閉じ、並びは自由
    // 略称では色の項目名を省いて[o2,green]と書ける
    // 閉じタグは正式名称と略称のどちらでもよく、[b]を[/bold]で閉じられる
    // [speed]は[s15]、[wait]は[w0.5]と略せる
    // [[と]]は角括弧そのもの
    [[nodiscard]] Text text(std::string_view text, TextConfig config = {}) const;

  private:
    friend class Asset;

    Font(const std::shared_ptr<Internal::FontData>& data, const FontConfig& config);

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

    // 最後のshow()の時点の値で、呼ぶ前はstarted
    // 一度に全部出す文は最初のshow()からfinished
    [[nodiscard]] Progress getProgress() const;

    // finishedで残りを一気に出し、startedで最初から出し直す
    // runningは何もしない
    void setProgress(Progress progress);

    [[nodiscard]] bool isFinished() const;

  private:
    friend class Font;

    Text(const std::weak_ptr<Internal::FontData>& data, std::shared_ptr<const Internal::TextScript> script, int size, bool antialias, Anchor anchor);

    // 出す文字の数を返す
    std::size_t advance();

    std::weak_ptr<Internal::FontData> data_;

    // 変わらないのでコピーと共有する
    std::shared_ptr<const Internal::TextScript> script_;

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
}
