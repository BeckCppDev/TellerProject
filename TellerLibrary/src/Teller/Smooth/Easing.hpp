#pragma once

namespace Teller
{
  // .smooth()の変化の仕方
  enum class Easing
  {
    // 一定の速さで動く
    linear = 0,
    // 最初が速く、最後がゆっくり
    easeOutCubic = 1,
    // 最初と最後がゆっくりで、中ほどが速い
    easeInOutCubic = 2,
    // 最初がゆっくりで、最後が速い
    easeInCubic = 3,
    // 目標を少し行き過ぎてから戻る
    easeOutBack = 4,
  };
}
