#include <Teller/Random/Random.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <random>

// 標準ライブラリの分布はコンパイラごとに結果が違うので、TASの再現性のため全て自前で行う

namespace Teller::Internal
{
  namespace
  {
    // xoshiro256**の状態
    using Generator = std::array<std::uint64_t, 4>;

    // SplitMix64
    std::uint64_t nextSeedWord(std::uint64_t& seed)
    {
      seed += 0x9E3779B97F4A7C15ULL;
      std::uint64_t word = seed;
      word = (word ^ (word >> 30)) * 0xBF58476D1CE4E5B9ULL;
      word = (word ^ (word >> 27)) * 0x94D049BB133111EBULL;
      return word ^ (word >> 31);
    }

    std::uint64_t nextWord(Generator& state)
    {
      const std::uint64_t result = std::rotl(state[1] * 5, 7) * 9;
      const std::uint64_t shifted = state[1] << 17;
      state[2] ^= state[0];
      state[3] ^= state[1];
      state[1] ^= state[2];
      state[0] ^= state[3];
      state[2] ^= shifted;
      state[3] = std::rotl(state[3], 45);
      return result;
    }

    void applySeed(Generator& state, std::uint64_t seed)
    {
      for (std::uint64_t& word : state)
      {
        word = nextSeedWord(seed);
      }
    }

    // OSから来る値はここを通す
    // TASの再生時に記録した値へ差し替える箇所
    std::uint64_t readOsSeed()
    {
      std::random_device device;
      const std::uint64_t upper = device();
      const std::uint64_t lower = device();
      return (upper << 32) | lower;
    }

    // グローバルなオブジェクトの初期化から呼ばれても使えるよう、関数内の静的変数にする
    Generator& generator()
    {
      static Generator state = []
      {
        Generator initial{};
        applySeed(initial, readOsSeed());
        return initial;
      }();
      return state;
    }

    // doubleの精度に合わせて上位53ビットを使う
    std::uint64_t nextMantissa()
    {
      return nextWord(generator()) >> 11;
    }
  }

  std::uint64_t randomUpTo(std::uint64_t maximum)
  {
    // 全範囲のときは候補の数が64ビットに収まらない
    if (maximum == (std::numeric_limits<std::uint64_t>::max)())
    {
      return nextWord(generator());
    }

    // 余りの偏りを防ぐため、端数の分は引き直す
    const std::uint64_t range = maximum + 1;
    const std::uint64_t threshold = (0 - range) % range;
    for (;;)
    {
      const std::uint64_t word = nextWord(generator());
      if (word >= threshold)
      {
        return word % range;
      }
    }
  }

  double randomUnit()
  {
    // 最大値で割ることで1.0も出るようにする
    constexpr double maximum = 0x1.0p53 - 1.0;
    return static_cast<double>(nextMantissa()) / maximum;
  }

  bool randomChance(double percent)
  {
    if (percent < 0.0)
    {
      percent = 0.0;
    }
    else if (percent > 100.0)
    {
      percent = 100.0;
    }

    // 確率の値で以後の並びが変わらないよう、0%と100%でも乱数を1つ進める
    // 100%で必ずtrueになるよう1.0が出ない値を使う
    const double unit = static_cast<double>(nextMantissa()) * 0x1.0p-53;
    return unit < percent / 100.0;
  }
}

namespace Teller
{
  void setRandomSeed(std::uint64_t seed)
  {
    Internal::applySeed(Internal::generator(), seed);
  }
}
