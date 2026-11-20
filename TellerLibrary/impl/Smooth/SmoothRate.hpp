#pragma once

#include <Teller/Units/FrameRate.hpp>

#include <optional>

namespace Teller::Internal
{
  // .smooth()の変化を1秒に何回進めるかの設定で、WindowとAudioが持つ
  class SmoothRateSetting
  {
  public:
    // ownerは警告の先頭に出すクラスの名前
    explicit SmoothRateSetting(const char* owner) noexcept;

    // 0以下の頻度は警告を出し、指定がなかったものとして扱う
    void set(std::optional<FrameRate> rate);

    void reset() noexcept;

    // 指定していないときはlogicの頻度を返す
    [[nodiscard]] int fps() const;

  private:
    const char* owner_;
    std::optional<FrameRate> rate_;
  };
}
