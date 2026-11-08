#pragma once

#include <cstddef>
#include <source_location>
#include <string_view>

namespace Teller
{
  // Asset::image()などに渡すパス
  // リリースビルドで埋め込むファイルをソースから拾うため、文字列リテラルだけを受け取る
  class AssetPath
  {
  public:
    // locationの既定値はimage()を書いた場所になる
    template <std::size_t N>
    consteval AssetPath(const char (&path)[N], std::source_location location = std::source_location::current())
      : path_(path, N - 1)
      , location_(location)
    {
    }

    [[nodiscard]] constexpr std::string_view path() const noexcept
    {
      return path_;
    }

    [[nodiscard]] constexpr const std::source_location& location() const noexcept
    {
      return location_;
    }

  private:
    std::string_view path_;
    std::source_location location_;
  };
}
