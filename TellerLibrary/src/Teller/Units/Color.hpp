#pragma once

#include <concepts>
#include <cstdint>

namespace Teller
{
  // 0xRRGGBBの整数か、Color::blackなどの定数で指定する
  class Color
  {
  public:
    // std::uint32_tで受けると、波括弧でintの変数を渡したときに縮小変換のエラーになる
    // 0xFFFFFFを超える桁は無視する
    template <std::integral T>
      requires(!std::same_as<T, bool>)
    constexpr Color(T rgb) noexcept
      : rgb_(static_cast<std::uint32_t>(rgb) & 0xFFFFFFu)
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
    std::uint32_t rgb_;
  };

  inline constexpr Color Color::black{ 0x000000 };
  inline constexpr Color Color::white{ 0xFFFFFF };
  inline constexpr Color Color::red{ 0xFF0000 };
  inline constexpr Color Color::green{ 0x00FF00 };
  inline constexpr Color Color::blue{ 0x0000FF };
  inline constexpr Color Color::yellow{ 0xFFFF00 };
  inline constexpr Color Color::cyan{ 0x00FFFF };
  inline constexpr Color Color::magenta{ 0xFF00FF };
}
