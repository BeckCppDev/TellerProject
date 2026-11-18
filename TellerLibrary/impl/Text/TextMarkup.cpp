#include <Text/TextMarkup.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace Teller::Internal
{
  namespace
  {
    // 長い待ちを切り詰めて桁あふれを防ぐ
    constexpr std::int64_t maxNanoseconds = 1'000'000'000'000'000'000;
    constexpr std::int64_t maxFrames = 1'000'000'000'000'000'000;
    constexpr std::uint64_t nanosecondsPerSecond = 1'000'000'000;

    std::int64_t addClamped(std::int64_t a, std::int64_t b, std::int64_t limit)
    {
      if (a >= limit - b)
      {
        return limit;
      }
      return a + b;
    }

    // 壊れたバイト列は1バイトずつU+FFFDにする
    char32_t nextCodepoint(std::string_view text, std::size_t& index)
    {
      constexpr char32_t replacement = 0xFFFD;
      const auto byte = [&text](std::size_t at)
      {
        return static_cast<unsigned char>(text[at]);
      };

      const unsigned char first = byte(index);
      int length = 0;
      char32_t codepoint = 0;
      if (first < 0x80)
      {
        ++index;
        return first;
      }
      if ((first & 0xE0) == 0xC0)
      {
        length = 2;
        codepoint = first & 0x1F;
      }
      else if ((first & 0xF0) == 0xE0)
      {
        length = 3;
        codepoint = first & 0x0F;
      }
      else if ((first & 0xF8) == 0xF0)
      {
        length = 4;
        codepoint = first & 0x07;
      }
      else
      {
        ++index;
        return replacement;
      }

      if (index + length > text.size())
      {
        ++index;
        return replacement;
      }
      for (int i = 1; i < length; ++i)
      {
        const unsigned char next = byte(index + i);
        if ((next & 0xC0) != 0x80)
        {
          ++index;
          return replacement;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
      }

      // 冗長な書き方、サロゲート、範囲外の値を弾く
      constexpr char32_t minimums[] = { 0, 0, 0x80, 0x800, 0x10000 };
      if (codepoint < minimums[length] || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
      {
        ++index;
        return replacement;
      }
      index += length;
      return codepoint;
    }

    // 同じ内容は1度だけ出す
    void warnMarkup(const char* reason, std::string_view tag)
    {
#ifndef NDEBUG
      static std::unordered_set<std::string> warned;
      std::string key(reason);
      key += '\n';
      key += tag;
      if (!warned.insert(key).second)
      {
        return;
      }
      const std::string shown(tag);
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Text: %s: %s", reason, shown.c_str());
#else
      (void)reason;
      (void)tag;
#endif
    }

    // 0以下と無限大は間隔なし
    // 丸めると誤差が積み重なり、割り切れる速さでも1フレーム遅れるので切り捨てる
    std::int64_t letterInterval(double lps)
    {
      if (!(lps > 0.0) || !std::isfinite(lps))
      {
        return 0;
      }
      const double interval = static_cast<double>(nanosecondsPerSecond) / lps;
      if (interval >= static_cast<double>(maxNanoseconds))
      {
        return maxNanoseconds;
      }
      return static_cast<std::int64_t>(interval);
    }

    // 1.5_secを1.5とsecに分ける
    // 負の数と無限大は失敗にする
    bool splitValue(std::string_view value, double& number, std::string_view& unit)
    {
      const std::size_t underscore = value.find('_');
      const std::string_view digits = value.substr(0, underscore);
      unit = underscore == std::string_view::npos ? std::string_view() : value.substr(underscore + 1);

      // from_charsはロケールに左右されない
      const char* last = digits.data() + digits.size();
      const auto [end, error] = std::from_chars(digits.data(), last, number);
      if (digits.empty() || error != std::errc() || end != last)
      {
        return false;
      }
      return std::isfinite(number) && number >= 0.0;
    }

    struct TagAlias
    {
      std::string_view shortName;
      std::string_view fullName;
    };

    constexpr TagAlias tagAliases[] = {
      { "b", "bold" },
      { "i", "italic" },
      { "u", "underline" },
      { "st", "strikethrough" },
      { "o", "outline" },
      { "s", "speed" },
      { "w", "wait" },
      { "c", "color" },
      { "e", "effect" },
      { "sh", "shake" },
      { "cr", "circle" },
      { "bb", "bubble" },
      { "af", "afterimage" },
      { "sz", "size" },
    };

    bool isAbbreviation(std::string_view name)
    {
      for (const TagAlias& alias : tagAliases)
      {
        if (name == alias.shortName)
        {
          return true;
        }
      }
      return false;
    }

    // [b]を[/bold]で閉じられるよう略称を正式名称に直す
    std::string_view fullTagName(std::string_view name)
    {
      for (const TagAlias& alias : tagAliases)
      {
        if (name == alias.shortName)
        {
          return alias.fullName;
        }
      }
      return name;
    }

    // 色の名前はTeller::Colorの定数と同じ綴り
    struct NamedColor
    {
      std::string_view name;
      std::uint32_t rgb;
    };

    constexpr NamedColor namedColors[] = {
      { "black", 0x000000 },
      { "white", 0xFFFFFF },
      { "red", 0xFF0000 },
      { "green", 0x00FF00 },
      { "blue", 0x0000FF },
      { "yellow", 0xFFFF00 },
      { "cyan", 0x00FFFF },
      { "magenta", 0xFF00FF },
    };

    int hexDigit(char c)
    {
      if (c >= '0' && c <= '9')
      {
        return c - '0';
      }
      if (c >= 'A' && c <= 'F')
      {
        return c - 'A' + 10;
      }
      if (c >= 'a' && c <= 'f')
      {
        return c - 'a' + 10;
      }
      return -1;
    }

    // 色の名前か#RRGGBBかrainbow
    bool parseColor(std::string_view text, TextColor& color)
    {
      if (text == "rainbow")
      {
        color = { 0xFFFFFF, true };
        return true;
      }
      for (const NamedColor& named : namedColors)
      {
        if (text == named.name)
        {
          color = { named.rgb, false };
          return true;
        }
      }
      if (text.size() != 7 || text.front() != '#')
      {
        return false;
      }
      std::uint32_t value = 0;
      for (std::size_t i = 1; i < text.size(); ++i)
      {
        const int digit = hexDigit(text[i]);
        if (digit < 0)
        {
          return false;
        }
        value = value * 16 + static_cast<std::uint32_t>(digit);
      }
      color = { value, false };
      return true;
    }

    std::string_view trimSpaces(std::string_view text)
    {
      while (!text.empty() && text.front() == ' ')
      {
        text.remove_prefix(1);
      }
      while (!text.empty() && text.back() == ' ')
      {
        text.remove_suffix(1);
      }
      return text;
    }

    bool isAsciiLetter(char c)
    {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    struct TagParts
    {
      std::string_view name;
      std::string_view value;
      bool hasValue = false;
    };

    // =があれば前後で分ける
    // 略称の直後に値を続けた[s10]は先頭の英字を名前にし、[speed10]は受け付けない
    TagParts splitTag(std::string_view body)
    {
      const std::size_t equals = body.find('=');
      if (equals != std::string_view::npos)
      {
        return { body.substr(0, equals), body.substr(equals + 1), true };
      }
      std::size_t letters = 0;
      while (letters < body.size() && isAsciiLetter(body[letters]))
      {
        ++letters;
      }
      const std::string_view head = body.substr(0, letters);
      if (letters < body.size() && isAbbreviation(head))
      {
        return { head, body.substr(letters), true };
      }
      return { body, std::string_view(), false };
    }

    // 縁取りのタグ1つ分
    struct OutlineLayer
    {
      int width = 0;
      TextColor color{ 0x000000 };
    };

    // 値を省略すると1px
    // 小数は切り捨てる
    bool readOutlineWidth(const TagParts& parts, int& width)
    {
      if (!parts.hasValue)
      {
        width = 1;
        return true;
      }
      double number = 0.0;
      std::string_view unit;
      if (!splitValue(parts.value, number, unit) || !(unit.empty() || unit == "px") || number > maxOutline)
      {
        return false;
      }
      width = static_cast<int>(number);
      return true;
    }

    // 単位は省略でき、unitが空なら単位を書けない
    bool readNumber(std::string_view item, std::string_view unit, bool allowNegative, double& number)
    {
      const std::size_t underscore = item.find('_');
      const std::string_view digits = item.substr(0, underscore);
      const char* last = digits.data() + digits.size();
      const auto [end, error] = std::from_chars(digits.data(), last, number);
      if (digits.empty() || error != std::errc() || end != last || !std::isfinite(number))
      {
        return false;
      }
      if (!allowNegative && number < 0.0)
      {
        return false;
      }
      if (underscore == std::string_view::npos)
      {
        return true;
      }
      return !unit.empty() && item.substr(underscore + 1) == unit;
    }

    // カンマで分け、前後の空白を除く
    std::vector<std::string_view> splitItems(std::string_view values)
    {
      std::vector<std::string_view> items;
      std::size_t start = 0;
      while (start <= values.size())
      {
        const std::size_t comma = std::min(values.find(',', start), values.size());
        items.push_back(trimSpaces(values.substr(start, comma - start)));
        start = comma + 1;
      }
      return items;
    }

    // [effect=shake]と[shake]のどちらでも書ける演出
    enum class EffectKind
    {
      shake,
      circle,
      bubble,
      afterimage,
    };

    struct EffectName
    {
      std::string_view name;
      EffectKind kind;
    };

    // 略称はtagAliasesで正式名称に直してから引く
    constexpr EffectName effectNames[] = {
      { "shake", EffectKind::shake },
      { "circle", EffectKind::circle },
      { "bubble", EffectKind::bubble },
      { "afterimage", EffectKind::afterimage },
    };

    bool findEffect(std::string_view fullName, EffectKind& kind)
    {
      for (const EffectName& effect : effectNames)
      {
        if (fullName == effect.name)
        {
          kind = effect.kind;
          return true;
        }
      }
      return false;
    }

    // 値を省略したときの既定値
    constexpr double defaultShake = 1.0;

    // 本家の揺れの40番
    constexpr TextCircle defaultCircle{ 2.0, 0.0, 20.0 };

    // 本家の揺れの43番(敵の吹き出し)
    constexpr TextCircle bubbleCircle{ 1.4, 30.0, 20.0 };

    // 本家のアズリエル登場時の名前
    constexpr TextAfterimage defaultAfterimage{ 24.0, 7 };

    // 省略した後ろの値は既定値のまま残す
    bool readCircle(const std::vector<std::string_view>& items, TextCircle& circle)
    {
      if (items.size() > 3)
      {
        return false;
      }
      if (!items.empty() && !readNumber(items[0], "px", false, circle.radius))
      {
        return false;
      }
      if (items.size() > 1 && !readNumber(items[1], "deg", true, circle.letterStep))
      {
        return false;
      }
      return items.size() <= 2 || readNumber(items[2], "deg", true, circle.turn);
    }

    bool readAfterimage(const std::vector<std::string_view>& items, TextAfterimage& look)
    {
      if (items.size() > 2)
      {
        return false;
      }
      if (!items.empty() && !readNumber(items[0], "px", false, look.spread))
      {
        return false;
      }
      if (items.size() < 2)
      {
        return true;
      }
      // 枚数の小数は切り捨てる
      double layers = 0.0;
      if (!readNumber(items[1], "", false, layers) || layers < 1.0 || layers >= maxAfterimageLayers + 1.0)
      {
        return false;
      }
      look.layers = static_cast<int>(layers);
      return true;
    }

    struct MarkupReader
    {
      TextScript script;
      TextMoment cursor;

      // [/speed]で1つ前に戻せるよう積む
      // 一番下はフォントの.speed
      std::vector<std::int64_t> intervals;

      // フォントの設定
      TextStyle baseStyle;

      // 開いたままの数で、重ねたときは全て閉じるまで効かせる
      int boldDepth = 0;
      int italicDepth = 0;
      int underlineDepth = 0;
      int strikethroughDepth = 0;

      // 一番下はフォントの設定
      std::vector<OutlineLayer> outlines;

      // 一番下は白
      std::vector<TextColor> colors;

      // 一番下はフォントの設定
      std::vector<double> sizes;

      // 開いた順に並べ、[/effect]で閉じる演出を決める
      std::vector<EffectKind> openEffects;

      // 重ねたときは内側の値を使う
      std::vector<double> shakes;
      std::vector<TextCircle> circles;
      std::vector<TextAfterimage> afterimages;

      // 値を持たないので開いている数だけ数える
      int bubbleDepth = 0;

      // 1文字目の前には間隔を空けない
      bool hasLetter = false;

      // 警告の文に使う
      std::string_view markup;

      std::span<const TextValue> values;
      std::size_t nextValue = 0;

      // タグの中も含めた{}の数
      std::size_t placeholders = 0;

      // 値が残っていなければ{}をそのまま描く
      void addValue()
      {
        ++placeholders;
        if (nextValue >= values.size())
        {
          addLetter(U'{');
          addLetter(U'}');
          return;
        }
        const TextValue& value = values[nextValue++];
        if (value.isColor)
        {
          warnMarkup("a Color can be put only in {} inside a tag", markup);
          return;
        }
        // 値の中の[と]はタグとして読まない
        std::size_t index = 0;
        while (index < value.text.size())
        {
          addLetter(nextCodepoint(value.text, index));
        }
      }

      // {}を色の#RRGGBBに置き換える
      // 色でない値か足りない値があればfalseを返す
      bool fillTagValues(std::string_view body, std::string& filled)
      {
        bool usable = true;
        std::size_t start = 0;
        while (true)
        {
          const std::size_t open = body.find("{}", start);
          if (open == std::string_view::npos)
          {
            filled += body.substr(start);
            return usable;
          }
          filled += body.substr(start, open - start);
          start = open + 2;
          ++placeholders;

          // 足りない値は最後に{}の数の警告で知らせる
          if (nextValue >= values.size())
          {
            usable = false;
            continue;
          }
          const TextValue& value = values[nextValue++];
          if (!value.isColor)
          {
            warnMarkup("only a Color can be put in {} inside a tag", body);
            usable = false;
            continue;
          }
          constexpr char digits[] = "0123456789ABCDEF";
          filled += '#';
          for (int shift = 20; shift >= 0; shift -= 4)
          {
            filled += digits[(value.rgb >> shift) & 0xF];
          }
        }
      }

      [[nodiscard]] TextStyle currentStyle() const
      {
        TextStyle style;
        style.bold = baseStyle.bold || boldDepth > 0;
        style.italic = baseStyle.italic || italicDepth > 0;
        style.underline = baseStyle.underline || underlineDepth > 0;
        style.strikethrough = baseStyle.strikethrough || strikethroughDepth > 0;
        style.outline = outlines.back().width;
        style.outlineColor = outlines.back().color;
        style.color = colors.back();
        style.size = sizes.back();
        style.shake = shakes.empty() ? 0.0 : shakes.back();
        if (!circles.empty())
        {
          style.circle = circles.back();
        }
        if (bubbleDepth > 0)
        {
          style.bubble = bubbleCircle;
        }
        if (!afterimages.empty())
        {
          style.afterimage = afterimages.back();
        }
        return style;
      }

      // itemsは演出の名前の後ろに並べた値
      void openEffect(EffectKind kind, const std::vector<std::string_view>& items, std::string_view body)
      {
        switch (kind)
        {
          case EffectKind::shake:
          {
            double amplitude = defaultShake;
            if (items.size() > 1 || (items.size() == 1 && !readNumber(items[0], "px", false, amplitude)))
            {
              warnMarkup("[shake] needs a value like 3_px", body);
              return;
            }
            shakes.push_back(amplitude);
            break;
          }
          case EffectKind::circle:
          {
            TextCircle circle = defaultCircle;
            if (!readCircle(items, circle))
            {
              warnMarkup("[circle] needs values like 2_px,10_deg,20_deg", body);
              return;
            }
            circles.push_back(circle);
            break;
          }
          case EffectKind::bubble:
            if (!items.empty())
            {
              warnMarkup("[bubble] takes no value", body);
              return;
            }
            ++bubbleDepth;
            break;
          case EffectKind::afterimage:
          {
            TextAfterimage look = defaultAfterimage;
            if (!readAfterimage(items, look))
            {
              warnMarkup("[afterimage] needs values like 24_px,7", body);
              return;
            }
            afterimages.push_back(look);
            break;
          }
        }
        openEffects.push_back(kind);
      }

      // 最後に開いたkindの演出を閉じる
      // 開いていなければfalseを返す
      bool closeEffect(EffectKind kind)
      {
        const auto found = std::find(openEffects.rbegin(), openEffects.rend(), kind);
        if (found == openEffects.rend())
        {
          return false;
        }
        openEffects.erase(std::next(found).base());
        switch (kind)
        {
          case EffectKind::shake:
            shakes.pop_back();
            break;
          case EffectKind::circle:
            circles.pop_back();
            break;
          case EffectKind::bubble:
            --bubbleDepth;
            break;
          case EffectKind::afterimage:
            afterimages.pop_back();
            break;
        }
        return true;
      }

      // [effect=shake,3_px]の形
      void applyEffectTag(std::string_view body, const TagParts& parts)
      {
        if (!parts.hasValue)
        {
          warnMarkup("[effect] needs an effect name like [effect=shake]", body);
          return;
        }
        std::vector<std::string_view> items = splitItems(parts.value);
        EffectKind kind{};
        if (!findEffect(fullTagName(items.front()), kind))
        {
          warnMarkup("unknown effect", body);
          return;
        }
        items.erase(items.begin());
        openEffect(kind, items, body);
      }

      // 値を持たない見た目のタグでなければnullptr
      int* depthOf(std::string_view name)
      {
        if (name == "bold")
        {
          return &boldDepth;
        }
        if (name == "italic")
        {
          return &italicDepth;
        }
        if (name == "underline")
        {
          return &underlineDepth;
        }
        if (name == "strikethrough")
        {
          return &strikethroughDepth;
        }
        return nullptr;
      }

      void addLetter(char32_t letter)
      {
        if (letter == U'\r')
        {
          return;
        }
        // 改行は時間を使わない
        if (letter != U'\n')
        {
          if (hasLetter)
          {
            cursor.nanoseconds = addClamped(cursor.nanoseconds, intervals.back(), maxNanoseconds);
          }
          hasLetter = true;
        }
        script.letters.push_back(letter);
        script.appearAt.push_back(cursor);
        script.styles.push_back(currentStyle());
      }

      void applyClosingTag(std::string_view body)
      {
        const std::string_view name = fullTagName(body.substr(1));
        if (name == "effect")
        {
          if (openEffects.empty())
          {
            warnMarkup("closing tag without opening tag", body);
            return;
          }
          closeEffect(openEffects.back());
          return;
        }
        if (EffectKind kind{}; findEffect(name, kind))
        {
          if (!closeEffect(kind))
          {
            warnMarkup("closing tag without opening tag", body);
          }
          return;
        }
        if (name == "speed" || name == "outline" || name == "color" || name == "size")
        {
          auto pop = [&body](auto& stack)
          {
            if (stack.size() > 1)
            {
              stack.pop_back();
            }
            else
            {
              warnMarkup("closing tag without opening tag", body);
            }
          };
          if (name == "speed")
          {
            pop(intervals);
          }
          else if (name == "outline")
          {
            pop(outlines);
          }
          else if (name == "size")
          {
            pop(sizes);
          }
          else
          {
            pop(colors);
          }
          return;
        }
        if (int* depth = depthOf(name))
        {
          if (*depth > 0)
          {
            --*depth;
          }
          else
          {
            warnMarkup("closing tag without opening tag", body);
          }
          return;
        }
        if (name == "wait")
        {
          warnMarkup("[wait] has no closing tag", body);
          return;
        }
        warnMarkup("unknown tag", body);
      }

      // [color=green,outline=2_px]や[o2,green]で、並びは自由
      // 縁取りのタグとして積む
      void applyOutlineWithColor(std::string_view body)
      {
        bool hasOutline = false;
        bool hasColor = false;
        int width = 0;
        TextColor color;

        std::size_t start = 0;
        while (start <= body.size())
        {
          const std::size_t comma = std::min(body.find(',', start), body.size());
          const std::string_view item = trimSpaces(body.substr(start, comma - start));
          start = comma + 1;

          const TagParts parts = splitTag(item);
          const std::string_view name = fullTagName(parts.name);
          if (name == "outline")
          {
            if (hasOutline)
            {
              warnMarkup("[outline] is written twice", body);
              return;
            }
            if (!readOutlineWidth(parts, width))
            {
              warnMarkup("[outline] needs a value like 1_px", body);
              return;
            }
            hasOutline = true;
            continue;
          }

          // 項目名のない色も受け付ける
          const bool namedItem = name == "color";
          TextColor parsed;
          if ((namedItem && parts.hasValue && parseColor(parts.value, parsed)) || (!namedItem && parseColor(item, parsed)))
          {
            if (hasColor)
            {
              warnMarkup("[color] is written twice", body);
              return;
            }
            hasColor = true;
            color = parsed;
            continue;
          }
          if (namedItem)
          {
            warnMarkup("[color] needs a color like white or #FFFFFF", body);
            return;
          }
          warnMarkup("only [outline] and [color] can be joined with a comma", body);
          return;
        }

        if (!hasOutline)
        {
          warnMarkup("a tag joined with a comma needs [outline]", body);
          return;
        }
        // 色を省くと外側の縁の色を引き継ぐ
        outlines.push_back({ width, hasColor ? color : outlines.back().color });
      }

      // bodyは角括弧の内側
      void applyTag(std::string_view body)
      {
        if (!body.empty() && body.front() == '/')
        {
          applyClosingTag(body);
          return;
        }

        const TagParts parts = splitTag(body);
        const std::string_view name = fullTagName(parts.name);

        // 演出の値のカンマを縁と色の区切りと取り違えないよう先に調べる
        if (name == "effect")
        {
          applyEffectTag(body, parts);
          return;
        }
        if (EffectKind kind{}; findEffect(name, kind))
        {
          openEffect(kind, parts.hasValue ? splitItems(parts.value) : std::vector<std::string_view>(), body);
          return;
        }

        if (body.find(',') != std::string_view::npos)
        {
          applyOutlineWithColor(body);
          return;
        }

        if (name == "size")
        {
          double number = 0.0;
          if (!parts.hasValue || !readNumber(parts.value, "px", false, number) || !(number > 0.0) || number > maxFontSize)
          {
            warnMarkup("[size] needs a value greater than 0 like 24_px", body);
            return;
          }
          sizes.push_back(number);
          return;
        }

        if (name == "speed")
        {
          double number = 0.0;
          std::string_view unit;
          if (!parts.hasValue || !splitValue(parts.value, number, unit) || !(unit.empty() || unit == "lps"))
          {
            warnMarkup("[speed] needs a value like 15_lps", body);
            return;
          }
          intervals.push_back(letterInterval(number));
          return;
        }

        if (name == "wait")
        {
          double number = 0.0;
          std::string_view unit;
          if (!parts.hasValue || !splitValue(parts.value, number, unit))
          {
            warnMarkup("[wait] needs a value like 1_sec, 500_ms or 10_frames", body);
            return;
          }
          if (unit.empty() || unit == "sec" || unit == "ms")
          {
            const double scaled = number * (unit == "ms" ? 1'000'000.0 : 1'000'000'000.0);
            const std::int64_t nanoseconds = scaled >= static_cast<double>(maxNanoseconds) ? maxNanoseconds : std::llround(scaled);
            cursor.nanoseconds = addClamped(cursor.nanoseconds, nanoseconds, maxNanoseconds);
            return;
          }
          if (unit == "frames")
          {
            // フレーム数は整数に限る
            if (number != std::floor(number))
            {
              warnMarkup("[wait] in frames needs a whole number", body);
              return;
            }
            const std::int64_t frames = number >= static_cast<double>(maxFrames) ? maxFrames : static_cast<std::int64_t>(number);
            cursor.frames = addClamped(cursor.frames, frames, maxFrames);
            return;
          }
          warnMarkup("[wait] needs a value like 1_sec, 500_ms or 10_frames", body);
          return;
        }

        if (name == "outline")
        {
          int width = 0;
          if (!readOutlineWidth(parts, width))
          {
            warnMarkup("[outline] needs a value like 1_px", body);
            return;
          }
          // 色は外側の縁の色を引き継ぐ
          outlines.push_back({ width, outlines.back().color });
          return;
        }

        // outlineのない[color]は文字の色
        if (name == "color")
        {
          TextColor parsed;
          if (!parts.hasValue || !parseColor(parts.value, parsed))
          {
            warnMarkup("[color] needs a color like white or #FFFFFF", body);
            return;
          }
          colors.push_back(parsed);
          return;
        }

        // 略称の[green]と[#00FF00]と[rainbow]
        // 閉じるのは[/color]か[/c]だけ
        if (TextColor parsed; parseColor(body, parsed))
        {
          colors.push_back(parsed);
          return;
        }

        if (int* depth = depthOf(name))
        {
          if (parts.hasValue)
          {
            warnMarkup("this tag takes no value", body);
            return;
          }
          ++*depth;
          return;
        }

        // 未対応のタグは読み飛ばす
        warnMarkup("unknown tag", body);
      }
    };
  }

  TextScript parseTextMarkup(std::string_view text, double speed, const TextStyle& baseStyle, std::span<const TextValue> values, bool checkValueCount)
  {
    MarkupReader reader;
    reader.intervals.push_back(letterInterval(speed));
    reader.baseStyle = baseStyle;
    reader.outlines.push_back({ baseStyle.outline, baseStyle.outlineColor });
    reader.colors.push_back(baseStyle.color);
    reader.sizes.push_back(baseStyle.size);
    reader.markup = text;
    reader.values = values;

    std::size_t index = 0;
    while (index < text.size())
    {
      const char current = text[index];
      const bool doubled = index + 1 < text.size() && text[index + 1] == current;

      // [[と]]は角括弧そのもの、{{と}}は波括弧そのものを表す
      if ((current == '[' || current == ']' || current == '{' || current == '}') && doubled)
      {
        reader.addLetter(static_cast<char32_t>(current));
        index += 2;
        continue;
      }

      // {}には次の値を入れる
      if (current == '{' && index + 1 < text.size() && text[index + 1] == '}')
      {
        reader.addValue();
        index += 2;
        continue;
      }

      if (current == '[')
      {
        const std::size_t close = text.find(']', index + 1);
        if (close == std::string_view::npos)
        {
          // 閉じていない[は文字として描く
          warnMarkup("'[' is not closed. Write [[ to show '['", text.substr(index));
          reader.addLetter(U'[');
          ++index;
          continue;
        }
        const std::string_view body = text.substr(index + 1, close - index - 1);
        index = close + 1;
        if (body.find("{}") == std::string_view::npos)
        {
          reader.applyTag(body);
          continue;
        }
        // 色を入れられなかったタグは無視する
        std::string filled;
        if (reader.fillTagValues(body, filled))
        {
          reader.applyTag(filled);
        }
        continue;
      }

      // 対のない]、{、}も文字として描く
      reader.addLetter(nextCodepoint(text, index));
    }

    if (checkValueCount && reader.placeholders != values.size())
    {
      warnMarkup("the number of {} and the number of values do not match", text);
    }

    reader.script.end = reader.cursor;
    return reader.script;
  }

  double clampFontSize(double size)
  {
    if (!(size > 0.0))
    {
      return 0.0;
    }
    return std::min(size, maxFontSize);
  }

  std::uint64_t momentToFrames(const TextMoment& moment, int fps)
  {
    const std::uint64_t rate = fps > 0 ? static_cast<std::uint64_t>(fps) : 1;

    // 桁あふれを防ぐため秒の整数部と端数に分けて掛け、切り上げる
    const auto nanoseconds = static_cast<std::uint64_t>(moment.nanoseconds);
    const std::uint64_t wholeSeconds = nanoseconds / nanosecondsPerSecond;
    const std::uint64_t restScaled = nanoseconds % nanosecondsPerSecond * rate;
    const std::uint64_t framesFromSeconds = wholeSeconds * rate + restScaled / nanosecondsPerSecond + (restScaled % nanosecondsPerSecond != 0 ? 1 : 0);
    return framesFromSeconds + static_cast<std::uint64_t>(moment.frames);
  }
}
