#pragma once

#include <Graphics/DrawBus.hpp>
#include <Text/FontFile.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>

namespace Teller::Internal
{
  // 文字の絵の使い回しのキー
  struct GlyphKey
  {
    int glyph = 0;
    int size = 0;
    GlyphStyle style;

    // 0xRRGGBB
    std::uint32_t color = 0;

    [[nodiscard]] bool operator==(const GlyphKey&) const noexcept = default;
  };

  struct GlyphKeyHash
  {
    std::size_t operator()(const GlyphKey& key) const noexcept
    {
      std::size_t hash = std::hash<int>{}(key.glyph);
      const auto mix = [&hash](std::size_t value)
      {
        hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
      };
      mix(std::hash<int>{}(key.size));
      mix(std::hash<int>{}(key.style.outline));
      mix(std::hash<std::uint32_t>{}(key.color));
      mix((key.style.antialias ? 1u : 0u) | (key.style.bold ? 2u : 0u) | (key.style.italic ? 4u : 0u));
      return hash;
    }
  };

  // 下線と取り消し線の四角の使い回しのキー
  struct LineImageKey
  {
    int width = 0;
    int height = 0;
    std::uint32_t color = 0;

    [[nodiscard]] auto operator<=>(const LineImageKey&) const noexcept = default;
  };

  // 1文字分の絵と位置
  struct CachedGlyph
  {
    // 空白などの何も描かない文字はnullptr
    std::shared_ptr<ImageData> image;
    int left = 0;
    int top = 0;
    int advance = 0;
  };

  // Asset::font()で指定したフォントのファイル
  struct FontData
  {
    FontData(const void* source, std::filesystem::path writtenPath, std::filesystem::path sourceDirectory);

    // 文字の絵の送り主にも使う
    const void* source;

    std::filesystem::path writtenPath;
    std::filesystem::path sourceDirectory;

    // 読み込むまではnullptr
    std::unique_ptr<FontFile> file;

    // 文の最初のshow()からの自動の読み込みを1回に限る
    bool loadAttempted = false;

    std::unordered_map<GlyphKey, CachedGlyph, GlyphKeyHash> glyphs;
    std::map<LineImageKey, std::shared_ptr<ImageData>> lineImages;
  };

  // 失敗したときはログを出してfalseを返す
  bool loadFont(FontData& font);
}
