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
#include <cstdint>
#include <iterator>
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
    // 濃さを透明度にした白い絵を作り、色は描くときに掛ける
    std::shared_ptr<ImageData> makeAlphaImage(const void* source, const unsigned char* alpha, int width, int height)
    {
      if (width <= 0 || height <= 0)
      {
        return nullptr;
      }
      SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
      if (surface == nullptr)
      {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Font: SDL_CreateSurface failed: %s", SDL_GetError());
        return nullptr;
      }
      for (int y = 0; y < height; ++y)
      {
        auto* row = static_cast<unsigned char*>(surface->pixels) + static_cast<std::ptrdiff_t>(y) * surface->pitch;
        for (int x = 0; x < width; ++x)
        {
          unsigned char* pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
          pixel[0] = 255;
          pixel[1] = 255;
          pixel[2] = 255;
          pixel[3] = alpha == nullptr ? 255 : alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
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
      cached.image = makeAlphaImage(font.source, bitmap.alpha.data(), bitmap.width, bitmap.height);
      cached.left = bitmap.left;
      cached.top = bitmap.top;
      cached.advance = bitmap.advance;
      return font.glyphs.emplace(key, std::move(cached)).first->second;
    }

    // 下線と取り消し線は1文字ごとに送り幅の長さの四角を描いてつなげる
    std::shared_ptr<ImageData> findLineImage(FontData& font, int width, int height)
    {
      const LineImageKey key{ width, height };
      if (const auto found = font.lineImages.find(key); found != font.lineImages.end())
      {
        return found->second;
      }
      std::shared_ptr<ImageData> image = makeAlphaImage(font.source, nullptr, width, height);
      font.lineImages.emplace(key, image);
      return image;
    }

    // 位置は文全体の箱の左上から見た絵の左上で、yは下向き
    struct PlacedImage
    {
      std::shared_ptr<ImageData> image;
      int x = 0;
      int y = 0;
      int line = 0;

      // TextScript::lettersの何文字目か
      std::size_t letter = 0;

      // 虹色の色はshow()で決める
      TextColor color;
    };

    struct Layout
    {
      // 隣の文字の中身を隠さないよう全ての中身より奥に描く
      std::vector<PlacedImage> rings;

      // 文字の中身と線
      std::vector<PlacedImage> fills;

      std::vector<int> lineWidths;
      int width = 0;
      int height = 0;
    };

    // まだ出していない文字も含めて並べる
    // 縁取りは送り幅と行の高さを変えないので、縁の有無で文字の位置はずれない
    Layout layOut(FontData& font, const TextScript& script, int size, bool antialias)
    {
      const LineMetrics metrics = font.file->lineMetrics(size);
      Layout layout;
      int penX = 0;
      int line = 0;
      int previousGlyph = -1;

      for (std::size_t index = 0; index < script.letters.size(); ++index)
      {
        const char32_t codepoint = script.letters[index];
        if (codepoint == U'\n')
        {
          layout.lineWidths.push_back(penX);
          penX = 0;
          ++line;
          previousGlyph = -1;
          continue;
        }
        const TextStyle& textStyle = script.styles[index];
        GlyphStyle style;
        style.antialias = antialias;
        style.bold = textStyle.bold;
        style.italic = textStyle.italic;

        const int glyph = font.file->glyphIndex(codepoint);
        if (previousGlyph >= 0)
        {
          penX += font.file->kerning(previousGlyph, glyph, size);
        }
        const int lineTop = line * metrics.lineSkip;
        const int baseline = lineTop + metrics.ascent;

        // findGlyphの戻り値は、後で別の文字を探してもunordered_mapの仕様で無効にならない
        const CachedGlyph& cached = findGlyph(font, glyph, size, style);
        if (cached.image != nullptr)
        {
          layout.fills.push_back({ cached.image, penX + cached.left, baseline + cached.top, line, index, textStyle.color });
        }
        if (textStyle.outline > 0)
        {
          GlyphStyle ringStyle = style;
          ringStyle.outline = textStyle.outline;
          const CachedGlyph& ring = findGlyph(font, glyph, size, ringStyle);
          if (ring.image != nullptr)
          {
            layout.rings.push_back({ ring.image, penX + ring.left, baseline + ring.top, line, index, textStyle.outlineColor });
          }
        }

        // 空白にも線を引く
        if ((textStyle.underline || textStyle.strikethrough) && cached.advance > 0)
        {
          const LineMetrics ringMetrics = textStyle.outline > 0 ? font.file->lineMetrics(size, textStyle.outline) : metrics;
          const auto addLine = [&](int fillTop, int ringTop)
          {
            layout.fills.push_back({ findLineImage(font, cached.advance, metrics.lineThickness), penX, lineTop + fillTop, line, index, textStyle.color });
            if (textStyle.outline > 0)
            {
              // 縁の分だけ左右にも広げる
              const int ringWidth = cached.advance + 2 * textStyle.outline;
              layout.rings.push_back({ findLineImage(font, ringWidth, ringMetrics.lineThickness), penX - textStyle.outline, lineTop + ringTop, line, index, textStyle.outlineColor });
            }
          };
          if (textStyle.underline)
          {
            addLine(metrics.underlineTop, ringMetrics.underlineTop);
          }
          if (textStyle.strikethrough)
          {
            addLine(metrics.strikethroughTop, ringMetrics.strikethroughTop);
          }
        }

        penX += cached.advance;
        previousGlyph = glyph;
      }
      layout.lineWidths.push_back(penX);

      layout.width = *std::max_element(layout.lineWidths.begin(), layout.lineWidths.end());
      layout.height = line * metrics.lineSkip + metrics.height;
      return layout;
    }

    constexpr std::uint64_t rainbowSeconds = 2;

    // timer()のフレーム数から0xRRGGBBで返す
    // TASで再現できるよう整数だけで計算する
    std::uint32_t rainbowColor()
    {
      const int fps = clockFps();
      const std::uint64_t period = rainbowSeconds * static_cast<std::uint64_t>(fps > 0 ? fps : 1);

      // 赤、黄、緑、水色、青、紫の6区間を255段階ずつ進む
      const std::uint64_t position = clockFrames() % period * 6 * 255 / period;
      const auto section = static_cast<int>(position / 255);
      const auto rising = static_cast<std::uint32_t>(position % 255);
      const std::uint32_t falling = 255 - rising;

      std::uint32_t r = 0;
      std::uint32_t g = 0;
      std::uint32_t b = 0;
      switch (section)
      {
        case 0:
          r = 255;
          g = rising;
          break;
        case 1:
          r = falling;
          g = 255;
          break;
        case 2:
          g = 255;
          b = rising;
          break;
        case 3:
          g = falling;
          b = 255;
          break;
        case 4:
          r = rising;
          b = 255;
          break;
        default:
          r = 255;
          b = falling;
          break;
      }
      return (r << 16) | (g << 8) | b;
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
    , bold_(config.bold)
    , italic_(config.italic)
    , underline_(config.underline)
    , strikethrough_(config.strikethrough)
    , outline_(std::clamp(config.outline.px(), 0, Internal::maxOutline))
    , outlineColor_((static_cast<std::uint32_t>(config.outlineColor.r()) << 16) | (static_cast<std::uint32_t>(config.outlineColor.g()) << 8) | config.outlineColor.b())
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
    Internal::TextStyle baseStyle;
    baseStyle.bold = bold_;
    baseStyle.italic = italic_;
    baseStyle.underline = underline_;
    baseStyle.strikethrough = strikethrough_;
    baseStyle.outline = outline_;
    baseStyle.outlineColor = { outlineColor_, false };
    auto source = std::make_shared<Internal::TextSource>();
    source->markup = text;
    source->speed = speed_;
    source->baseStyle = baseStyle;
    return Text(data_, std::move(source), size, antialias_, anchor_);
  }

  Progress Font::showDirect(Position position, Anchor anchor, std::string_view markup, std::vector<Internal::TextValue> values)
  {
    // 周期が進まず忘れる時機がないので覚えない
    if (!Internal::isClockActive())
    {
      Text once = text(markup);
      return once.showWithValues(position, anchor, std::move(values));
    }

    // 前の周期に渡されなかった文字列を忘れる
    const std::uint64_t frame = Internal::clockFrames();
    std::erase_if(shownTexts_, [frame](const ShownText& shown)
    {
      return shown.lastFrame + 1 < frame;
    });

    auto found = std::find_if(shownTexts_.begin(), shownTexts_.end(), [markup](const ShownText& shown)
    {
      return shown.markup == markup;
    });
    if (found == shownTexts_.end())
    {
      shownTexts_.push_back({ std::string(markup), text(markup), frame });
      found = std::prev(shownTexts_.end());
    }
    found->lastFrame = frame;

    return found->text.showWithValues(position, anchor, std::move(values));
  }

  Text::Text(const std::weak_ptr<Internal::FontData>& data, std::shared_ptr<const Internal::TextSource> source, int size, bool antialias, Anchor anchor)
    : data_(data)
    , source_(std::move(source))
    , size_(size)
    , antialias_(antialias)
    , anchor_(anchor)
  {
    script_ = std::make_shared<const Internal::TextScript>(Internal::parseTextMarkup(source_->markup, source_->speed, source_->baseStyle));
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
    return showWithValues(position, anchor, {});
  }

  Progress Text::showWithValues(Position position, Anchor anchor, std::vector<Internal::TextValue> values)
  {
    // 進み具合はshownFrames_に残るので、読み直しても続きから出る
    if (source_ != nullptr && (!parsedForShow_ || values != values_))
    {
      script_ = std::make_shared<const Internal::TextScript>(Internal::parseTextMarkup(source_->markup, source_->speed, source_->baseStyle, values, true));
      values_ = std::move(values);
      parsedForShow_ = true;
    }

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

    const Internal::Layout layout = Internal::layOut(*data, *script_, size_, antialias_);

    // にじまないよう、箱の左上を整数のピクセルに揃える
    // 利用者の座標はYが上向き
    const int halvesX = Internal::horizontalHalves(anchor);
    const int halvesY = Internal::verticalHalves(anchor);
    const double left = std::floor(position.x() - layout.width * halvesX / 2.0 + 0.5);
    const double top = std::floor(position.y() + layout.height * halvesY / 2.0 + 0.5);

    const std::uint32_t rainbow = Internal::rainbowColor();

    const auto publish = [&](const std::vector<Internal::PlacedImage>& images)
    {
      for (const Internal::PlacedImage& placed : images)
      {
        if (placed.letter >= visibleLetters || placed.image == nullptr)
        {
          continue;
        }
        const int lineOffset = (layout.width - layout.lineWidths[static_cast<std::size_t>(placed.line)]) * halvesX / 2;
        const double width = placed.image->surface->w;
        const double height = placed.image->surface->h;

        // Windowは画像の中心の位置を受け取る
        const double centerX = left + lineOffset + placed.x + width / 2.0;
        const double centerY = top - placed.y - height / 2.0;
        const std::uint32_t tint = placed.color.rainbow ? rainbow : placed.color.rgb;
        Internal::publishDraw({ placed.image, centerX, centerY, tint });
      }
    };
    publish(layout.rings);
    publish(layout.fills);
    return progress_;
  }
}
