#pragma once

#include <cstddef>
#include <cstdint>

namespace Teller
{
  // "#RRGGBB"の文字列か、Color::blackなどの定数で指定する
  class Color
  {
  public:
    // 16進数の大文字と小文字のどちらでもよい
    // 形が違うとコンパイルエラーになる
    template <std::size_t N>
    consteval Color(const char (&code)[N])
      : rgb_(parseCode(code, N))
    {
    }

    [[nodiscard]] constexpr std::uint8_t r() const noexcept
    {
      return static_cast<std::uint8_t>(rgb_ >> 16);
    }

    [[nodiscard]] constexpr std::uint8_t g() const noexcept
    {
      return static_cast<std::uint8_t>(rgb_ >> 8);
    }

    [[nodiscard]] constexpr std::uint8_t b() const noexcept
    {
      return static_cast<std::uint8_t>(rgb_);
    }

    [[nodiscard]] constexpr bool operator==(const Color&) const noexcept = default;

    // 自身の型の静的メンバはクラスの中で定義できないので外で定義する
    static const Color black;
    static const Color white;
    static const Color red;
    static const Color green;
    static const Color blue;
    static const Color yellow;
    static const Color cyan;
    static const Color magenta;

  private:
    // constevalの中でthrowに達すると、その呼び出しがコンパイルエラーになる
    static consteval std::uint32_t hexDigit(char c)
    {
      if (c >= '0' && c <= '9')
      {
        return static_cast<std::uint32_t>(c - '0');
      }
      if (c >= 'A' && c <= 'F')
      {
        return static_cast<std::uint32_t>(c - 'A' + 10);
      }
      if (c >= 'a' && c <= 'f')
      {
        return static_cast<std::uint32_t>(c - 'a' + 10);
      }
      throw "Color code needs hexadecimal digits like #7A80C3";
    }

    // 長さは末尾のヌル文字を含めて8
    static consteval std::uint32_t parseCode(const char* code, std::size_t length)
    {
      if (length != 8 || code[0] != '#')
      {
        throw "Color code needs the form #RRGGBB";
      }
      std::uint32_t rgb = 0;
      for (std::size_t i = 1; i < 7; ++i)
      {
        rgb = rgb * 16 + hexDigit(code[i]);
      }
      return rgb;
    }

    std::uint32_t rgb_;
  };

  inline constexpr Color Color::black{ "#000000" };
  inline constexpr Color Color::white{ "#FFFFFF" };
  inline constexpr Color Color::red{ "#FF0000" };
  inline constexpr Color Color::green{ "#00FF00" };
  inline constexpr Color Color::blue{ "#0000FF" };
  inline constexpr Color Color::yellow{ "#FFFF00" };
  inline constexpr Color Color::cyan{ "#00FFFF" };
  inline constexpr Color Color::magenta{ "#FF00FF" };
}
