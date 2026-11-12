#pragma once

#include <Teller/Units/Literals.hpp>

#include <concepts>

namespace Teller
{
  // 1秒あたりの文字数
  // 単位のない数値はlpsとして扱い、小数も受け付ける
  // 0以下は一度に全部出す
  class LetterRate
  {
  public:
    // trueを1lpsとして受け付けない
    // doubleで受けると、波括弧でintの変数を渡したときに縮小変換のエラーになる
    template <typename T>
      requires (std::integral<T> || std::floating_point<T>) && (!std::same_as<T, bool>)
    constexpr LetterRate(T lps) noexcept
      : lps_(static_cast<double>(lps))
    {
    }

    [[nodiscard]] constexpr double lps() const noexcept
    {
      return lps_;
    }

    [[nodiscard]] constexpr bool operator==(const LetterRate&) const noexcept = default;

  private:
    double lps_;
  };

  namespace literals
  {
    consteval LetterRate operator""_lps(unsigned long long value)
    {
      return LetterRate(value);
    }

    consteval LetterRate operator""_lps(long double value)
    {
      return LetterRate(static_cast<double>(value));
    }
  }
}
