#pragma once

#include <cstdint>
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

  // 文字の絵の大きさが桁あふれしないよう縁の太さを抑える
  constexpr int maxOutline = 1'000;

  // 文字や縁の色
  struct TextColor
  {
    // 0xRRGGBB
    std::uint32_t rgb = 0xFFFFFF;

    // trueなら描く周期ごとに色が変わり、rgbは使わない
    bool rainbow = false;
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

  // speedは1秒あたりの文字数で、0以下なら一度に出す
  // baseStyleはタグのない部分の見た目
  [[nodiscard]] TextScript parseTextMarkup(std::string_view text, double speed, const TextStyle& baseStyle);

  // 最初のshow()を0として、その時刻に達する最初のフレームを返す
  [[nodiscard]] std::uint64_t momentToFrames(const TextMoment& moment, int fps);
}
