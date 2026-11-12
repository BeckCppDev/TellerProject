#include <Teller/Asset/Font.hpp>

#include <Asset/AssetFiles.hpp>
#include <Asset/FontData.hpp>
#include <Graphics/DrawBus.hpp>
#include <Loop/ClockInternal.hpp>
#include <Text/TextMarkup.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Teller::Internal
{
  FontData::FontData(const void* source, std::filesystem::path writtenPath, std::filesystem::path sourceDirectory)
    : source(source)
    , writtenPath(std::move(writtenPath))
    , sourceDirectory(std::move(sourceDirectory))
  {
  }

  bool loadFont(FontData& font)
  {
    font.loadAttempted = true;
    if (font.file != nullptr)
    {
      return true;
    }

    const std::string resolved = resolveAssetPath(font.writtenPath, font.sourceDirectory);
    std::size_t size = 0;
    void* bytes = SDL_LoadFile(resolved.c_str(), &size);
    if (bytes == nullptr)
    {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Font: failed to load %s: %s", resolved.c_str(), SDL_GetError());
      return false;
    }
    const auto* begin = static_cast<const unsigned char*>(bytes);
    std::vector<unsigned char> copied(begin, begin + size);
    SDL_free(bytes);

    font.file = FontFile::open(std::move(copied));
    if (font.file == nullptr)
    {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Font: %s is not a TrueType or OpenType font", resolved.c_str());
      return false;
    }
    return true;
  }

  namespace
  {
    // 白で、濃さを透明度にする
    std::shared_ptr<ImageData> makeGlyphImage(const void* source, const GlyphBitmap& bitmap)
    {
      if (bitmap.width <= 0 || bitmap.height <= 0)
      {
        return nullptr;
      }
      SDL_Surface* surface = SDL_CreateSurface(bitmap.width, bitmap.height, SDL_PIXELFORMAT_RGBA32);
      if (surface == nullptr)
      {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Font: SDL_CreateSurface failed: %s", SDL_GetError());
        return nullptr;
      }
      for (int y = 0; y < bitmap.height; ++y)
      {
        auto* row = static_cast<unsigned char*>(surface->pixels) + static_cast<std::ptrdiff_t>(y) * surface->pitch;
        for (int x = 0; x < bitmap.width; ++x)
        {
          unsigned char* pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
          pixel[0] = 255;
          pixel[1] = 255;
          pixel[2] = 255;
          pixel[3] = bitmap.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width) + static_cast<std::size_t>(x)];
        }
      }

      auto image = std::make_shared<ImageData>(source, std::filesystem::path(), std::filesystem::path());
      image->surface = surface;
      image->loadAttempted = true;
      return image;
    }

    const CachedGlyph& findGlyph(FontData& font, int glyph, int size, const GlyphStyle& style)
    {
      const GlyphKey key{ glyph, size, style };
      if (const auto found = font.glyphs.find(key); found != font.glyphs.end())
      {
        return found->second;
      }

      const GlyphBitmap bitmap = font.file->render(glyph, size, style);
      CachedGlyph cached;
      cached.image = makeGlyphImage(font.source, bitmap);
      cached.left = bitmap.left;
      cached.top = bitmap.top;
      cached.advance = bitmap.advance;
      return font.glyphs.emplace(key, std::move(cached)).first->second;
    }

    // 位置は文全体の箱の左上から見た絵の左上で、yは下向き
    struct PlacedGlyph
    {
      std::shared_ptr<ImageData> image;
      int x = 0;
      int y = 0;
      int line = 0;

      // TextScript::lettersの何文字目か
      std::size_t letter = 0;
    };

    struct Layout
    {
      std::vector<PlacedGlyph> glyphs;
      std::vector<int> lineWidths;
      int width = 0;
      int height = 0;
    };

    // まだ出していない文字も含めて並べる
    Layout layOut(FontData& font, std::u32string_view letters, int size, const GlyphStyle& style)
    {
      const LineMetrics metrics = font.file->lineMetrics(size, style.outline);
      Layout layout;
      int penX = 0;
      int line = 0;
      int previousGlyph = -1;

      for (std::size_t index = 0; index < letters.size(); ++index)
      {
        const char32_t codepoint = letters[index];
        if (codepoint == U'\n')
        {
          layout.lineWidths.push_back(penX);
          penX = 0;
          ++line;
          previousGlyph = -1;
          continue;
        }
        const int glyph = font.file->glyphIndex(codepoint);
        if (previousGlyph >= 0)
        {
          penX += font.file->kerning(previousGlyph, glyph, size);
        }
        const CachedGlyph& cached = findGlyph(font, glyph, size, style);
        if (cached.image != nullptr)
        {
          const int baseline = line * metrics.lineSkip + metrics.ascent;
          layout.glyphs.push_back({ cached.image, penX + cached.left, baseline + cached.top, line, index });
        }
        penX += cached.advance;
        previousGlyph = glyph;
      }
      layout.lineWidths.push_back(penX);

      layout.width = *std::max_element(layout.lineWidths.begin(), layout.lineWidths.end());
      layout.height = line * metrics.lineSkip + metrics.height;
      return layout;
    }

    // 箱の幅や高さに掛ける割合の2倍
    int horizontalHalves(Anchor anchor)
    {
      switch (anchor)
      {
        case Anchor::top:
        case Anchor::center:
        case Anchor::bottom:
          return 1;
        case Anchor::topRight:
        case Anchor::right:
        case Anchor::bottomRight:
          return 2;
        default:
          return 0;
      }
    }

    int verticalHalves(Anchor anchor)
    {
      switch (anchor)
      {
        case Anchor::left:
        case Anchor::center:
        case Anchor::right:
          return 1;
        case Anchor::bottomLeft:
        case Anchor::bottom:
        case Anchor::bottomRight:
          return 2;
        default:
          return 0;
      }
    }
  }
}

namespace Teller
{
  Font::Font(const std::shared_ptr<Internal::FontData>& data, const FontConfig& config)
    : data_(data)
    , size_(config.size.px())
    , speed_(config.speed.lps())
    , antialias_(config.antialias)
    , anchor_(config.anchor)
  {
  }

  bool Font::load() const
  {
    const std::shared_ptr<Internal::FontData> data = data_.lock();
    if (data == nullptr)
    {
      return false;
    }
    return Internal::loadFont(*data);
  }

  Text Font::text(std::string_view text, TextConfig config) const
  {
    const int size = config.size.has_value() ? config.size->px() : size_;
    return Text(data_, text, size, speed_, antialias_, anchor_);
  }

  Text::Text(const std::weak_ptr<Internal::FontData>& data, std::string_view text, int size, double speed, bool antialias, Anchor anchor)
    : data_(data)
    , script_(std::make_shared<const Internal::TextScript>(Internal::parseTextMarkup(text, speed)))
    , size_(size)
    , antialias_(antialias)
    , anchor_(anchor)
  {
  }

  std::size_t Text::advance()
  {
    if (script_ == nullptr)
    {
      progress_ = Progress::finished;
      return 0;
    }

    // LoopControllerがないと周期を数えられないので一度に全部出す
    if (!Internal::isClockActive())
    {
      skipped_ = true;
    }

    // show()を呼んだ周期だけを1回ずつ数える
    const std::uint64_t frame = Internal::clockFrames();
    if (!shown_)
    {
      shown_ = true;
      lastFrame_ = frame;
      shownFrames_ = 0;
    }
    else if (frame != lastFrame_)
    {
      lastFrame_ = frame;
      ++shownFrames_;
    }

    const Internal::TextScript& script = *script_;
    if (skipped_)
    {
      progress_ = Progress::finished;
      return script.letters.size();
    }

    // TASで再現できるよう整数で比べる
    const int fps = Internal::clockFps();
    const auto reached = [this, fps](const Internal::TextMoment& moment)
    {
      return Internal::momentToFrames(moment, fps) <= shownFrames_;
    };
    const auto firstHidden = std::partition_point(script.appearAt.begin(), script.appearAt.end(), reached);

    if (reached(script.end))
    {
      progress_ = Progress::finished;
    }
    else if (shownFrames_ == 0)
    {
      progress_ = Progress::started;
    }
    else
    {
      progress_ = Progress::running;
    }
    return static_cast<std::size_t>(firstHidden - script.appearAt.begin());
  }

  Progress Text::getProgress() const
  {
    return progress_;
  }

  void Text::setProgress(Progress progress)
  {
    switch (progress)
    {
      case Progress::started:
        // 次のshow()を最初のshow()として扱う
        shown_ = false;
        skipped_ = false;
        shownFrames_ = 0;
        progress_ = Progress::started;
        break;
      case Progress::finished:
        skipped_ = true;
        progress_ = Progress::finished;
        break;
      case Progress::running:
#ifndef NDEBUG
      {
        static bool warned = false;
        if (!warned)
        {
          warned = true;
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Text: setProgress(Progress::running) does nothing");
        }
      }
#endif
        break;
    }
  }

  bool Text::isFinished() const
  {
    return progress_ == Progress::finished;
  }

  Progress Text::show(Position position)
  {
    return show(position, anchor_);
  }

  Progress Text::show(Position position, Anchor anchor)
  {
    // ゲームの進行がフォントの有無に左右されないよう、描く前に進める
    const std::size_t visibleLetters = advance();

    const std::shared_ptr<Internal::FontData> data = data_.lock();
    if (data == nullptr)
    {
#ifndef NDEBUG
      static bool warned = false;
      if (!warned)
      {
        warned = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Text: show() was called on a text that was not made by Font::text() or whose Asset was destroyed");
      }
#endif
      return progress_;
    }

    // 毎周期読み直してログが溢れないよう、自動で読むのは初回だけにする
    if (data->file == nullptr)
    {
      if (data->loadAttempted || !Internal::loadFont(*data))
      {
        return progress_;
      }
    }
    if (size_ <= 0 || script_->letters.empty())
    {
      return progress_;
    }

    Internal::GlyphStyle style;
    style.antialias = antialias_;
    const Internal::Layout layout = Internal::layOut(*data, script_->letters, size_, style);

    // にじまないよう、箱の左上を整数のピクセルに揃える
    // 利用者の座標はYが上向き
    const int halvesX = Internal::horizontalHalves(anchor);
    const int halvesY = Internal::verticalHalves(anchor);
    const double left = std::floor(position.x() - layout.width * halvesX / 2.0 + 0.5);
    const double top = std::floor(position.y() + layout.height * halvesY / 2.0 + 0.5);

    for (const Internal::PlacedGlyph& glyph : layout.glyphs)
    {
      if (glyph.letter >= visibleLetters)
      {
        break;
      }
      const int lineOffset = (layout.width - layout.lineWidths[static_cast<std::size_t>(glyph.line)]) * halvesX / 2;
      const double width = glyph.image->surface->w;
      const double height = glyph.image->surface->h;

      // Windowは画像の中心の位置を受け取る
      const double centerX = left + lineOffset + glyph.x + width / 2.0;
      const double centerY = top - glyph.y - height / 2.0;
      Internal::publishDraw({ glyph.image, centerX, centerY });
    }
    return progress_;
  }
}
