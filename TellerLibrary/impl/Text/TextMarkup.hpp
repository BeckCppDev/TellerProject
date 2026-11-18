#pragma once

#include <Teller/Asset/Font.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 文字列の中の角括弧のタグを読み、どの文字をいつ出すかを決める
namespace Teller::Internal
{
  // 文の最初のshow()から数えた時刻
  // 秒とフレーム数の待ちを別々に持ち、logicのfpsでフレームに直す
  struct TextMoment
  {
    std::int64_t nanoseconds = 0;
    std::int64_t frames = 0;
  };

  // 文字の絵の大きさが桁あふれしないよう縁の太さと文字の大きさを抑える
  constexpr int maxOutline = 1'000;
  constexpr double maxFontSize = 1'000.0;

  // 描く絵の数が膨れ上がらないよう残像の枚数を抑える
  constexpr int maxAfterimageLayers = 64;

  // 0以下とNaNは0、maxFontSizeより大きい値はmaxFontSizeにする
  [[nodiscard]] double clampFontSize(double size);

  // 文字や縁の色
  struct TextColor
  {
    // 0xRRGGBB
    std::uint32_t rgb = 0xFFFFFF;

    // trueなら描く周期ごとに色が変わり、rgbは使わない
    bool rainbow = false;
  };

  // 本家の揺れの40から43番と同じく、円周上の点の分だけ文字をずらす
  struct TextCircle
  {
    // 0なら揺れない
    double radius = 0.0;

    // 度数で、1文字ごとの角度のずれ
    double letterStep = 0.0;

    // 度数で、演出の1周期ごとに回る角度
    double turn = 0.0;
  };

  // 同じ文字を薄くしながら重ね、1枚ごとにでたらめにずらす
  struct TextAfterimage
  {
    // いちばん外側の1枚が片側にずれる最大のピクセル数
    double spread = 0.0;

    // 0なら残像なし
    int layers = 0;
  };

  // 1文字ごとの見た目
  struct TextStyle
  {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;

    // 0なら縁取りなし
    int outline = 0;

    TextColor color{ 0xFFFFFF };
    TextColor outlineColor{ 0x000000 };

    // 1emのピクセル数で、0以下なら描かない
    double size = 16.0;

    // 片側に揺れる最大のピクセル数で、0なら揺れない
    double shake = 0.0;

    // [circle]と[bubble]は別々に持ち、両方あればずれを足し合わせる
    TextCircle circle;
    TextCircle bubble;

    TextAfterimage afterimage;
  };

  // タグを取り除いた文と各文字が出る時刻
  struct TextScript
  {
    // 改行は\nのまま入り、\rは取り除く
    std::u32string letters;

    // lettersと同じ並びで、後ろほど遅いか同じ
    std::vector<TextMoment> appearAt;

    // lettersと同じ並び
    std::vector<TextStyle> styles;

    // 最後の文字と、その後ろの[wait]が終わる時刻
    TextMoment end;
  };

  // Font::text()に渡した文字列とフォントの設定
  // {}の値が変わるたびに読み直す
  struct TextSource
  {
    std::string markup;
    double speed = 0.0;
    TextStyle baseStyle;
  };

  // speedは1秒あたりの文字数で、0以下なら一度に出す
  // baseStyleはタグのない部分の見た目
  // valuesは{}に出てきた順に入れる
  // checkValueCountがtrueなら{}と値の数の食い違いを警告する
  [[nodiscard]] TextScript parseTextMarkup(std::string_view text, double speed, const TextStyle& baseStyle, std::span<const TextValue> values = {}, bool checkValueCount = false);

  // 最初のshow()を0として、その時刻に達する最初のフレームを返す
  [[nodiscard]] std::uint64_t momentToFrames(const TextMoment& moment, int fps);
}
