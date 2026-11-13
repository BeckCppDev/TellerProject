#include <Text/TextMarkup.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>
#include <unordered_set>

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

    // 色の名前か#RRGGBB
    bool parseColor(std::string_view text, std::uint32_t& rgb)
    {
      for (const NamedColor& named : namedColors)
      {
        if (text == named.name)
        {
          rgb = named.rgb;
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
      rgb = value;
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
    // [cred]はcの後ろが色の名前のときに限り[c=red]として読む
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
      std::uint32_t unused = 0;
      if (body.size() > 1 && body.front() == 'c' && parseColor(body.substr(1), unused))
      {
        return { body.substr(0, 1), body.substr(1), true };
      }
      return { body, std::string_view(), false };
    }

    // 縁取りのタグ1つ分
    struct OutlineLayer
    {
      int width = 0;
      std::uint32_t color = 0;
    };

    // 値を省略すると1px
    bool readOutlineWidth(const TagParts& parts, int& width)
    {
      if (!parts.hasValue)
      {
        width = 1;
        return true;
      }
      double number = 0.0;
      std::string_view unit;
      if (!splitValue(parts.value, number, unit) || !(unit.empty() || unit == "px") || number != std::floor(number) || number > maxOutline)
      {
        return false;
      }
      width = static_cast<int>(number);
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
      std::vector<std::uint32_t> colors;

      // 1文字目の前には間隔を空けない
      bool hasLetter = false;

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
        return style;
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
        if (name == "speed" || name == "outline" || name == "color")
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
        std::uint32_t color = 0;

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
              warnMarkup("[outline] needs a whole number like 1_px", body);
              return;
            }
            hasOutline = true;
            continue;
          }

          // 項目名のない色も受け付ける
          const bool namedItem = name == "color";
          std::uint32_t rgb = 0;
          if ((namedItem && parts.hasValue && parseColor(parts.value, rgb)) || (!namedItem && parseColor(item, rgb)))
          {
            if (hasColor)
            {
              warnMarkup("[color] is written twice", body);
              return;
            }
            hasColor = true;
            color = rgb;
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
        if (body.find(',') != std::string_view::npos)
        {
          applyOutlineWithColor(body);
          return;
        }

        const TagParts parts = splitTag(body);
        const std::string_view name = fullTagName(parts.name);

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
            warnMarkup("[outline] needs a whole number like 1_px", body);
            return;
          }
          // 色は外側の縁の色を引き継ぐ
          outlines.push_back({ width, outlines.back().color });
          return;
        }

        // outlineのない[color]は文字の色
        if (name == "color")
        {
          std::uint32_t rgb = 0;
          if (!parts.hasValue || !parseColor(parts.value, rgb))
          {
            warnMarkup("[color] needs a color like white or #FFFFFF", body);
            return;
          }
          colors.push_back(rgb);
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

  TextScript parseTextMarkup(std::string_view text, double speed, const TextStyle& baseStyle)
  {
    MarkupReader reader;
    reader.intervals.push_back(letterInterval(speed));
    reader.baseStyle = baseStyle;
    reader.outlines.push_back({ baseStyle.outline, baseStyle.outlineColor });
    reader.colors.push_back(baseStyle.color);

    std::size_t index = 0;
    while (index < text.size())
    {
      const char current = text[index];
      const bool doubled = index + 1 < text.size() && text[index + 1] == current;

      // [[と]]は角括弧そのものを表す
      if ((current == '[' || current == ']') && doubled)
      {
        reader.addLetter(static_cast<char32_t>(current));
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
        reader.applyTag(text.substr(index + 1, close - index - 1));
        index = close + 1;
        continue;
      }

      // 対のない]も文字として描く
      reader.addLetter(nextCodepoint(text, index));
    }

    reader.script.end = reader.cursor;
    return reader.script;
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
