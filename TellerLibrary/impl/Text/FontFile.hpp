#pragma once

#include <memory>
#include <vector>

// stb_truetypeの型はFontFile.cppの外に出さない
namespace Teller::Internal
{
  struct GlyphStyle
  {
    bool antialias = false;
    bool bold = false;
    bool italic = false;

    // 0より大きいと、輪郭からこのピクセル数以内の環を描く
    int outline = 0;

    [[nodiscard]] bool operator==(const GlyphStyle&) const noexcept = default;
  };

  // 濃さは0から255
  struct GlyphBitmap
  {
    std::vector<unsigned char> alpha;
    int width = 0;
    int height = 0;

    // 原点から見た絵の左上で、yは下向き
    int left = 0;
    int top = 0;

    int advance = 0;
  };

  // 数え方はSDL3_ttfに合わせてある
  struct LineMetrics
  {
    int ascent = 0;

    // 下向きは負
    int descent = 0;

    int height = 0;
    int lineSkip = 0;

    // 行の上端から見た線の上端
    int underlineTop = 0;
    int strikethroughTop = 0;
    int lineThickness = 1;
  };

  class FontFile
  {
  public:
    // 読めなかったときはnullptr
    // .ttcは最初のフォントを使う
    [[nodiscard]] static std::unique_ptr<FontFile> open(std::vector<unsigned char> bytes);

    ~FontFile();

    FontFile(const FontFile&) = delete;
    FontFile& operator=(const FontFile&) = delete;

    // フォントにない文字は0
    [[nodiscard]] int glyphIndex(char32_t codepoint) const;

    [[nodiscard]] int kerning(int leftGlyph, int rightGlyph, int size) const;

    [[nodiscard]] LineMetrics lineMetrics(int size, int outline = 0) const;

    // その大きさの埋め込みのドット絵があれば輪郭より優先する
    [[nodiscard]] GlyphBitmap render(int glyph, int size, const GlyphStyle& style) const;

  private:
    struct Impl;

    explicit FontFile(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
  };
}
