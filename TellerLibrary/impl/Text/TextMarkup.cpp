#include <Text/TextMarkup.hpp>

#include <SDL3/SDL.h>

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

    struct MarkupReader
    {
      TextScript script;
      TextMoment cursor;

      // [/speed]で1つ前に戻せるよう積む
      // 一番下はフォントの.speed
      std::vector<std::int64_t> intervals;

      // 1文字目の前には間隔を空けない
      bool hasLetter = false;

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
      }

      // bodyは角括弧の内側
      void applyTag(std::string_view body)
      {
        if (!body.empty() && body.front() == '/')
        {
          const std::string_view name = body.substr(1);
          if (name == "speed")
          {
            if (intervals.size() > 1)
            {
              intervals.pop_back();
            }
            else
            {
              warnMarkup("[/speed] without [speed]", body);
            }
            return;
          }
          warnMarkup("unknown tag", body);
          return;
        }

        const std::size_t equals = body.find('=');
        const std::string_view name = body.substr(0, equals);
        const std::string_view value = equals == std::string_view::npos ? std::string_view() : body.substr(equals + 1);

        if (name == "speed")
        {
          double number = 0.0;
          std::string_view unit;
          if (equals == std::string_view::npos || !splitValue(value, number, unit) || !(unit.empty() || unit == "lps"))
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
          if (equals == std::string_view::npos || !splitValue(value, number, unit))
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

        // 未対応のタグは読み飛ばす
        warnMarkup("unknown tag", body);
      }
    };
  }

  TextScript parseTextMarkup(std::string_view text, double speed)
  {
    MarkupReader reader;
    reader.intervals.push_back(letterInterval(speed));

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
