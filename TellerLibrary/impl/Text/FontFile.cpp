#include <Text/FontFile.hpp>

#include <cmath>
#include <cstdlib>
#include <utility>

// 利用者が別に使うstb_truetypeと名前がぶつからないよう、このファイルの中だけで使う
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION

// C4505はファイルの終わりで出るので、ファイル全体で止める
#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
  #pragma warning(push, 0)
  #pragma warning(disable : 4505)
#endif

#include <stb/stb_truetype.h>

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(_MSC_VER)
  #pragma warning(pop)
  #pragma warning(disable : 4505)
#endif

namespace Teller::Internal
{
  namespace
  {
    // SDL3_ttfの斜体の傾き
    constexpr float italicSlant = 0x366A / 65536.0f;

    int ceilPx(float value)
    {
      return static_cast<int>(std::ceil(value));
    }

    int floorPx(float value)
    {
      return static_cast<int>(std::floor(value));
    }

    // FreeTypeと同じく、16.16の倍率で1/64ピクセル単位に丸めてから整数に丸める
    int unitsToPixels(const stbtt_fontinfo& info, int units, int size)
    {
      const long long unitsPerEm = ttUSHORT(info.data + info.head + 18);
      if (unitsPerEm == 0)
      {
        return 0;
      }
      const long long scale = ((static_cast<long long>(size) * 64 * 65536) + unitsPerEm / 2) / unitsPerEm;
      const long long magnitude = (std::llabs(static_cast<long long>(units)) * scale + 0x8000) >> 16;
      const long long value = units < 0 ? -magnitude : magnitude;
      return static_cast<int>((value + 32) >> 6);
    }
  }

  struct FontFile::Impl
  {
    // stbtt_fontinfoはbytesを指す
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};

    [[nodiscard]] float scale(int size) const
    {
      return stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(size));
    }
  };

  FontFile::FontFile(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
  {
  }

  FontFile::~FontFile() = default;

  std::unique_ptr<FontFile> FontFile::open(std::vector<unsigned char> bytes)
  {
    // 表の一覧より短いファイルを弾く
    if (bytes.size() < 12)
    {
      return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->bytes = std::move(bytes);
    const int offset = stbtt_GetFontOffsetForIndex(impl->bytes.data(), 0);
    if (offset < 0 || stbtt_InitFont(&impl->info, impl->bytes.data(), offset) == 0)
    {
      return nullptr;
    }
    return std::unique_ptr<FontFile>(new FontFile(std::move(impl)));
  }

  int FontFile::glyphIndex(char32_t codepoint) const
  {
    return stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint));
  }

  int FontFile::kerning(int leftGlyph, int rightGlyph, int size) const
  {
    const int units = stbtt_GetGlyphKernAdvance(&impl_->info, leftGlyph, rightGlyph);
    return unitsToPixels(impl_->info, units, size);
  }

  LineMetrics FontFile::lineMetrics(int size, int outline) const
  {
    const float scale = impl_->scale(size);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);

    LineMetrics metrics;
    metrics.ascent = ceilPx(static_cast<float>(ascent) * scale);
    metrics.descent = ceilPx(static_cast<float>(descent) * scale);
    metrics.height = ceilPx(static_cast<float>(ascent - descent) * scale);
    metrics.lineSkip = ceilPx(static_cast<float>(ascent - descent + lineGap) * scale);

    // FreeTypeと同じく、postの表の位置を線の中央に直す
    int underlinePosition = 0;
    int underlineThickness = 0;
    stbtt_GetUnderlineMetrics(&impl_->info, &underlinePosition, &underlineThickness);
    underlinePosition -= underlineThickness / 2;
    const int underlineOffset = floorPx(static_cast<float>(underlinePosition) * scale);
    metrics.lineThickness = floorPx(static_cast<float>(underlineThickness) * scale);
    if (metrics.lineThickness < 1)
    {
      metrics.lineThickness = 1;
    }

    metrics.underlineTop = metrics.ascent - underlineOffset - 1;
    metrics.strikethroughTop = metrics.height / 2;

    if (outline > 0)
    {
      metrics.lineThickness += 2 * outline;
      metrics.underlineTop -= outline;
      metrics.strikethroughTop -= outline;
    }
    if (metrics.underlineTop < 0)
    {
      metrics.underlineTop = 0;
    }
    if (metrics.strikethroughTop < 0)
    {
      metrics.strikethroughTop = 0;
    }
    return metrics;
  }

  GlyphBitmap FontFile::render(int glyph, int size, const GlyphStyle& style) const
  {
    stbtt_style stbStyle{};
    stbStyle.mono = style.antialias ? 0 : 1;
    stbStyle.slant = style.italic ? italicSlant : 0.0f;
    stbStyle.outline = static_cast<float>(style.outline);

    // SDL3_ttfと同じく大きさの10分の1
    const int embolden = style.bold ? size / 10 : 0;
    stbStyle.embolden = embolden;

    GlyphBitmap result;
    unsigned char* pixels = stbtt_GetGlyphEmbeddedBitmapStyled(&impl_->info, size, glyph, &stbStyle, &result.width, &result.height, &result.left, &result.top, &result.advance);
    if (result.advance < 0)
    {
      const float scale = impl_->scale(size);
      int advanceWidth = 0;
      int leftSideBearing = 0;
      stbtt_GetGlyphHMetrics(&impl_->info, glyph, &advanceWidth, &leftSideBearing);
      result.advance = unitsToPixels(impl_->info, advanceWidth, size);
      pixels = stbtt_GetGlyphBitmapStyled(&impl_->info, scale, scale, glyph, &stbStyle, &result.width, &result.height, &result.left, &result.top);
    }
    result.advance += embolden;

    if (pixels != nullptr)
    {
      result.alpha.assign(pixels, pixels + static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height));
      stbtt_FreeBitmap(pixels, nullptr);
    }
    else
    {
      result.width = 0;
      result.height = 0;
    }
    return result;
  }
}
