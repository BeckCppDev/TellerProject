#include <Teller/Asset/Font.hpp>

#include <Asset/AssetFiles.hpp>
#include <Asset/FontData.hpp>
#include <Graphics/DrawBus.hpp>
#include <Loop/ClockInternal.hpp>
#include <Teller/Random/Random.hpp>
#include <Text/TextMarkup.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <optional>
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

    const CachedGlyph& findGlyph(FontData& font, int glyph, double size, const GlyphStyle& style)
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

    // 大きさごとの行の寸法を使い回す
    // 1つの文に出てくる大きさは数種類なので順に探す
    class MetricsCache
    {
    public:
      explicit MetricsCache(const FontFile& file)
        : file_(file)
      {
      }

      [[nodiscard]] LineMetrics get(double size)
      {
        for (const auto& [cachedSize, metrics] : entries_)
        {
          if (cachedSize == size)
          {
            return metrics;
          }
        }
        entries_.emplace_back(size, file_.lineMetrics(size));
        return entries_.back().second;
      }

    private:
      const FontFile& file_;
      std::vector<std::pair<double, LineMetrics>> entries_;
    };

    // 行の高さを決める大きさを行ごとに返す
    // 文字のない行は、その行を終える改行の大きさを使う
    // lettersは空でないこと
    std::vector<double> lineSizes(const TextScript& script)
    {
      // 負の値はまだ文字がないことを表す
      std::vector<double> sizes(1, -1.0);
      for (std::size_t index = 0; index < script.letters.size(); ++index)
      {
        const double size = script.styles[index].size;
        if (script.letters[index] == U'\n')
        {
          if (sizes.back() < 0.0)
          {
            sizes.back() = size;
          }
          sizes.push_back(-1.0);
          continue;
        }
        sizes.back() = std::max(sizes.back(), size);
      }
      // 改行で終わる文の最後の空の行は、最後の改行の大きさを使う
      if (sizes.back() < 0.0)
      {
        sizes.back() = script.styles.back().size;
      }
      return sizes;
    }

    // まだ出していない文字も含めて並べる
    // 縁取りは送り幅と行の高さを変えないので、縁の有無で文字の位置はずれない
    // 大きさの違う文字は下の基準線でそろえ、行の高さはその行でいちばん大きい文字に合わせる
    // lettersは空でないこと
    Layout layOut(FontData& font, const TextScript& script, bool antialias)
    {
      MetricsCache cache(*font.file);
      Layout layout;

      // 行ごとの基準線を先に決める
      const std::vector<double> sizes = lineSizes(script);
      std::vector<int> baselines;
      int nextTop = 0;
      int lastTop = 0;
      for (const double size : sizes)
      {
        const LineMetrics lineMetrics = cache.get(size);
        lastTop = nextTop;
        baselines.push_back(nextTop + lineMetrics.ascent);
        nextTop += lineMetrics.lineSkip;
      }
      layout.height = lastTop + cache.get(sizes.back()).height;

      int penX = 0;
      int line = 0;
      int previousGlyph = -1;
      double previousSize = 0.0;

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
        const double size = textStyle.size;

        // 大きさが0の文字は描かず、幅も取らない
        if (!(size > 0.0))
        {
          previousGlyph = -1;
          continue;
        }
        GlyphStyle style;
        style.antialias = antialias;
        style.bold = textStyle.bold;
        style.italic = textStyle.italic;

        const int glyph = font.file->glyphIndex(codepoint);

        // 大きさの違う文字の間は詰めない
        if (previousGlyph >= 0 && previousSize == size)
        {
          penX += font.file->kerning(previousGlyph, glyph, size);
        }
        const LineMetrics metrics = cache.get(size);
        const int baseline = baselines[static_cast<std::size_t>(line)];

        // この文字の大きさで見た行の上端
        const int lineTop = baseline - metrics.ascent;

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
        previousSize = size;
      }
      layout.lineWidths.push_back(penX);

      layout.width = *std::max_element(layout.lineWidths.begin(), layout.lineWidths.end());
      return layout;
    }

    // 演出を動かす周期の通し番号
    // effectFpsがlogicより低いと、同じ番号が何周期か続く
    std::uint64_t effectTick(int effectFps)
    {
      const std::uint64_t frames = clockFrames();
      const int logic = clockFps();
      if (effectFps <= 0 || logic <= 0 || effectFps >= logic)
      {
        return frames;
      }
      const auto effect = static_cast<std::uint64_t>(effectFps);
      const auto logicFps = static_cast<std::uint64_t>(logic);

      // 桁あふれを防ぐため秒の整数部と端数に分けて掛ける
      return frames / logicFps * effect + frames % logicFps * effect / logicFps;
    }

    // 0以下の頻度は指定がなかったものとして扱う
    int validEffectFps(const std::optional<FrameRate>& rate)
    {
      if (!rate)
      {
        return 0;
      }
      if (rate->fps() > 0)
      {
        return rate->fps();
      }
#ifndef NDEBUG
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Font: effectRate must be greater than 0, so logic is used");
#endif
      return 0;
    }

    // 文字の描く位置に足すずれで、yは下向き
    struct Offset
    {
      double x = 0.0;
      double y = 0.0;
    };

    // 本家のhspeedとvspeedと同じく、角度0で右、90度で上へずれる
    // 角度は演出の周期から決まり、画面上の全ての文でそろう
    void addCircle(const TextCircle& circle, std::size_t letter, std::uint64_t tick, Offset& offset)
    {
      if (!(circle.radius > 0.0))
      {
        return;
      }
      // 精度を保つため1周の中に収める
      const double degrees = std::fmod(circle.turn * static_cast<double>(tick) + circle.letterStep * static_cast<double>(letter), 360.0);
      const double radians = degrees * std::numbers::pi / 180.0;
      offset.x += circle.radius * std::cos(radians);
      offset.y -= circle.radius * std::sin(radians);
    }

    // 残像の1枚分の重ね方
    struct AfterimageLayer
    {
      // -1から1のでたらめな値に掛けるピクセル数
      double reach = 0.0;

      // でたらめなずれの中心を上下へ寄せるピクセル数
      double biasY = 0.0;

      std::uint8_t alpha = 255;
    };

    // 本家のアズリエル登場時の名前の重ね方を、ずれの幅と枚数に合わせて伸び縮みさせる
    // 24px、7枚では本家と同じになる
    AfterimageLayer afterimageLayer(const TextAfterimage& look, int layer)
    {
      AfterimageLayer result;

      // 濃さは千分率
      int permille = 0;
      if (layer < 2)
      {
        // 内側の2枚は、いちばん外側の12分の1だけずれる
        result.reach = look.spread / 12.0;
        permille = layer == 0 ? 900 : 700;

        // 本家は2枚目だけ中心を少し上に寄せている
        if (layer == 1)
        {
          result.biasY = -look.spread / 24.0;
        }
      }
      else
      {
        // 3枚目から外側へ等間隔に広がり、いちばん外側の1枚がspreadになる
        result.reach = look.spread * layer / (look.layers - 1);

        // 濃さは500から100へ等間隔に下がる
        permille = look.layers <= 3 ? 500 : 500 - 400 * (layer - 2) / (look.layers - 3);
      }
      result.alpha = static_cast<std::uint8_t>((permille * 255 + 500) / 1000);
      return result;
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
    , size_(Internal::clampFontSize(config.size.px()))
    , speed_(config.speed.lps())
    , antialias_(config.antialias)
    , bold_(config.bold)
    , italic_(config.italic)
    , underline_(config.underline)
    , strikethrough_(config.strikethrough)
    , outline_(std::clamp(Internal::truncatedPx(config.outline), 0, Internal::maxOutline))
    , outlineColor_((static_cast<std::uint32_t>(config.outlineColor.r()) << 16) | (static_cast<std::uint32_t>(config.outlineColor.g()) << 8) | config.outlineColor.b())
    , anchor_(config.anchor)
    , effectFps_(Internal::validEffectFps(config.effectRate))
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
    const double size = config.size.has_value() ? Internal::clampFontSize(config.size->px()) : size_;
    Internal::TextStyle baseStyle;
    baseStyle.bold = bold_;
    baseStyle.italic = italic_;
    baseStyle.underline = underline_;
    baseStyle.strikethrough = strikethrough_;
    baseStyle.outline = outline_;
    baseStyle.outlineColor = { outlineColor_, false };
    baseStyle.size = size;
    auto source = std::make_shared<Internal::TextSource>();
    source->markup = text;
    source->speed = speed_;
    source->baseStyle = baseStyle;
    return Text(data_, std::move(source), antialias_, anchor_, effectFps_);
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

  Text::Text(const std::weak_ptr<Internal::FontData>& data, std::shared_ptr<const Internal::TextSource> source, bool antialias, Anchor anchor, int effectFps)
    : data_(data)
    , source_(std::move(source))
    , antialias_(antialias)
    , anchor_(anchor)
    , effectFps_(effectFps)
  {
    // タグの中の{}に色を入れられるよう、文字列は最初のshow()で値と一緒に読む
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

  void Text::updateEffects()
  {
    const Internal::TextScript& script = *script_;
    const std::uint64_t tick = Internal::effectTick(effectFps_);

    // 1周期に何回show()を呼んでも、ずれを決めるのは1回だけ
    if (hasEffectTick_ && tick == effectTick_)
    {
      // {}の値が変わって文字数が増えても乱数は進めず、増えた文字は次の周期まで揺らさない
      shakeOffsets_.resize(script.letters.size() * 2, 0.0);
      return;
    }
    hasEffectTick_ = true;
    effectTick_ = tick;

    // 乱数を進める回数は文字列だけで決まるよう、まだ出していない文字の分も決める
    // 演出のない文は乱数を進めない
    shakeOffsets_.assign(script.letters.size() * 2, 0.0);
    int layers = 0;
    for (std::size_t index = 0; index < script.letters.size(); ++index)
    {
      if (script.letters[index] == U'\n')
      {
        continue;
      }
      const Internal::TextStyle& style = script.styles[index];
      if (style.shake > 0.0)
      {
        shakeOffsets_[index * 2] = random<double>(-style.shake, style.shake);
        shakeOffsets_[index * 2 + 1] = random<double>(-style.shake, style.shake);
      }
      layers = std::max(layers, style.afterimage.layers);
    }

    // 残像は1枚ごとに全ての文字を同じだけずらす
    afterimageOffsets_.assign(static_cast<std::size_t>(layers) * 2, 0.0);
    for (double& offset : afterimageOffsets_)
    {
      offset = random<double>(-1.0, 1.0);
    }
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
    if (source_ != nullptr && (script_ == nullptr || values != values_))
    {
      script_ = std::make_shared<const Internal::TextScript>(Internal::parseTextMarkup(source_->markup, source_->speed, source_->baseStyle, values, true));
      values_ = std::move(values);
    }

    // ゲームの進行がフォントの有無に左右されないよう、描く前に進める
    const std::size_t visibleLetters = advance();

    // 乱数の並びもフォントの有無に左右されないよう、フォントを調べる前に進める
    if (script_ != nullptr)
    {
      updateEffects();
    }

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
    if (script_->letters.empty())
    {
      return progress_;
    }

    const Internal::TextScript& script = *script_;
    const Internal::Layout layout = Internal::layOut(*data, script, antialias_);

    // にじまないよう、箱の左上を整数のピクセルに揃える
    // 利用者の座標はYが上向き
    const int halvesX = Internal::horizontalHalves(anchor);
    const int halvesY = Internal::verticalHalves(anchor);
    const double left = std::floor(position.x() - layout.width * halvesX / 2.0 + 0.5);
    const double top = std::floor(position.y() + layout.height * halvesY / 2.0 + 0.5);

    const std::uint32_t rainbow = Internal::rainbowColor();

    // [shake]、[circle]、[bubble]のずれを足し合わせる
    // 縁取り、下線、取り消し線も、元の文字と同じだけずれる
    const auto motion = [&](std::size_t letter)
    {
      Internal::Offset offset;
      const Internal::TextStyle& style = script.styles[letter];
      if (style.shake > 0.0 && letter * 2 + 1 < shakeOffsets_.size())
      {
        offset.x += shakeOffsets_[letter * 2];
        offset.y += shakeOffsets_[letter * 2 + 1];
      }
      Internal::addCircle(style.circle, letter, effectTick_, offset);
      Internal::addCircle(style.bubble, letter, effectTick_, offset);
      return offset;
    };

    // ずれは1px単位に丸める
    const auto publishOne = [&](const Internal::PlacedImage& placed, const Internal::Offset& offset, std::uint8_t alpha)
    {
      const int lineOffset = (layout.width - layout.lineWidths[static_cast<std::size_t>(placed.line)]) * halvesX / 2;
      const double width = placed.image->surface->w;
      const double height = placed.image->surface->h;
      const double shiftX = std::floor(offset.x + 0.5);
      const double shiftY = std::floor(offset.y + 0.5);

      // Windowは画像の中心の位置を受け取る
      const double centerX = left + lineOffset + placed.x + shiftX + width / 2.0;
      const double centerY = top - placed.y - shiftY - height / 2.0;
      const std::uint32_t tint = placed.color.rainbow ? rainbow : placed.color.rgb;
      Internal::publishDraw({ placed.image, centerX, centerY, tint, alpha });
    };

    const auto publish = [&](const std::vector<Internal::PlacedImage>& images)
    {
      int layers = 0;
      for (const Internal::PlacedImage& placed : images)
      {
        if (placed.letter >= visibleLetters || placed.image == nullptr)
        {
          continue;
        }
        // 残像のある文字は、残像のない文字を全て描いた後に重ねる
        const int letterLayers = script.styles[placed.letter].afterimage.layers;
        if (letterLayers > 0)
        {
          layers = std::max(layers, letterLayers);
          continue;
        }
        publishOne(placed, motion(placed.letter), 255);
      }

      // 本家と同じく、1枚目を全ての文字で描いてから次の1枚を重ねる
      for (int layer = 0; layer < layers; ++layer)
      {
        const auto layerIndex = static_cast<std::size_t>(layer);
        const double randomX = layerIndex * 2 + 1 < afterimageOffsets_.size() ? afterimageOffsets_[layerIndex * 2] : 0.0;
        const double randomY = layerIndex * 2 + 1 < afterimageOffsets_.size() ? afterimageOffsets_[layerIndex * 2 + 1] : 0.0;
        for (const Internal::PlacedImage& placed : images)
        {
          if (placed.letter >= visibleLetters || placed.image == nullptr)
          {
            continue;
          }
          const Internal::TextAfterimage& look = script.styles[placed.letter].afterimage;
          if (layer >= look.layers)
          {
            continue;
          }
          const Internal::AfterimageLayer shape = Internal::afterimageLayer(look, layer);
          Internal::Offset offset = motion(placed.letter);
          offset.x += shape.reach * randomX;
          offset.y += shape.reach * randomY + shape.biasY;
          publishOne(placed, offset, shape.alpha);
        }
      }
    };
    publish(layout.rings);
    publish(layout.fills);
    return progress_;
  }
}
