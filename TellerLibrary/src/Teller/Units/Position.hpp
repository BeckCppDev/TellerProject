#pragma once

#include <Teller/Units/Length.hpp>

#include <concepts>

namespace Teller
{
  namespace Internal
  {
    // boolは数値のつもりで渡すことがないので除く
    template <typename T>
    concept PositionAxis = std::same_as<T, Length> || ((std::integral<T> || std::floating_point<T>) && !std::same_as<T, bool>);

    template <PositionAxis T>
    [[nodiscard]] constexpr double toPositionAxis(T value) noexcept
    {
      if constexpr (std::same_as<T, Length>)
      {
        return static_cast<double>(value.px());
      }
      else
      {
        return static_cast<double>(value);
      }
    }
  }

  // 画面の中央が{0, 0}で、Xは右向き、Yは上向きが正
  // 単位のない数値はpxとして扱う
  class Position
  {
  public:
    // doubleで受けると、波括弧でintの変数を渡したときに縮小変換のエラーになる
    template <Internal::PositionAxis X, Internal::PositionAxis Y>
    constexpr Position(X x, Y y) noexcept
      : x_(Internal::toPositionAxis(x))
      , y_(Internal::toPositionAxis(y))
    {
    }

    [[nodiscard]] constexpr double x() const noexcept
    {
      return x_;
    }

    [[nodiscard]] constexpr double y() const noexcept
    {
      return y_;
    }

    [[nodiscard]] constexpr bool operator==(const Position&) const noexcept = default;

    // 画面の中央
    // 自身の型の静的メンバはクラスの中で定義できないので外で定義する
    static const Position central;

  private:
    double x_;
    double y_;
  };

  inline constexpr Position Position::central{ 0, 0 };
}
