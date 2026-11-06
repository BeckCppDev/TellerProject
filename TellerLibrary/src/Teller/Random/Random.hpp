#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace Teller
{
  namespace Internal
  {
    // 0以上maximum以下の整数を返す
    [[nodiscard]] std::uint64_t randomUpTo(std::uint64_t maximum);

    // 0.0以上1.0以下の小数を返す
    [[nodiscard]] double randomUnit();

    // percentの確率でtrueを返す
    [[nodiscard]] bool randomChance(double percent);
  }

  // lower以上upper以下の値を返す
  // 例：Teller::random<int>(1, 6)は1〜6を返す
  // lowerとupperが逆のときは入れ替える
  template <typename T>
    requires (std::integral<T> || std::floating_point<T>) && (!std::same_as<T, bool>)
  [[nodiscard]] T random(T lower, T upper)
  {
    if (upper < lower)
    {
      std::swap(lower, upper);
    }

    if constexpr (std::integral<T>)
    {
      // 負の数を含む範囲でも幅を正しく求めるため、符号なしの型で計算する
      using Unsigned = std::make_unsigned_t<T>;
      const auto width = static_cast<Unsigned>(static_cast<Unsigned>(upper) - static_cast<Unsigned>(lower));
      if (width == 0)
      {
        return lower;
      }
      const auto offset = static_cast<Unsigned>(Internal::randomUpTo(width));
      return static_cast<T>(static_cast<Unsigned>(static_cast<Unsigned>(lower) + offset));
    }
    else
    {
      const T result = lower + static_cast<T>(static_cast<double>(upper - lower) * Internal::randomUnit());
      // 丸めの誤差で上の端を超えないようにする
      return result > upper ? upper : result;
    }
  }

  // percentの確率でvalueを、残りの確率で反対の値を返す
  // 例：Teller::random<bool>(true, 50)は50%の確率でtrueを返す
  // percentは小数でもよい。0未満は0、100より大きいときは100として丸める
  template <std::same_as<bool> T>
  [[nodiscard]] bool random(T value, double percent)
  {
    return Internal::randomChance(percent) ? value : !value;
  }

  // 同じ値を指定すると、その後は同じ並びになる
  // 指定しないときは起動ごとに変わる
  void setRandomSeed(std::uint64_t seed);
}
