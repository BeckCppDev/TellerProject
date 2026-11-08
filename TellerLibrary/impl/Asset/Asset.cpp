#include <Teller/Asset/Asset.hpp>

#include <Graphics/DrawBus.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace Teller::Internal
{
  struct AssetState
  {
    // キーは、書かれたパスをimage()を書いたcppのフォルダで補ったもの
    std::unordered_map<std::string, std::shared_ptr<ImageData>> images;
  };

  namespace
  {
    // SDL3の文字列はUTF-8
    // Windowsではstringのまま変換すると日本語のパスが化けるので、u8stringを通す
    std::filesystem::path toPath(std::string_view utf8)
    {
      return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    std::string toUtf8(const std::filesystem::path& path)
    {
      const std::u8string text = path.lexically_normal().u8string();
      return std::string(text.begin(), text.end());
    }

    // 配布先には開発時のフォルダがないので、実行ファイルの隣を優先する
    // 絶対パスは演算子/がそのまま返す
    std::string resolvePath(const ImageData& image)
    {
      if (const char* basePath = SDL_GetBasePath())
      {
        const std::string besideExecutable = toUtf8(toPath(basePath) / image.writtenPath);
        if (SDL_GetPathInfo(besideExecutable.c_str(), nullptr))
        {
          return besideExecutable;
        }
      }
      return toUtf8(image.sourceDirectory / image.writtenPath);
    }

    bool loadImage(ImageData& image)
    {
      image.loadAttempted = true;
      if (image.surface != nullptr)
      {
        return true;
      }

      const std::string resolved = resolvePath(image);
      image.surface = SDL_LoadPNG(resolved.c_str());
      if (image.surface == nullptr)
      {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Image: failed to load %s: %s", resolved.c_str(), SDL_GetError());
        return false;
      }
      return true;
    }
  }
}

namespace Teller
{
  void Internal::AssetStateDeleter::operator()(AssetState* state) const noexcept
  {
    delete state;
  }

  Asset::~Asset()
  {
    Internal::notifySourceReleased(this);
  }

  // コンストラクタを定数初期化にするため、最初に使うときに作る
  Internal::AssetState& Asset::state()
  {
    if (state_ == nullptr)
    {
      state_.reset(new Internal::AssetState);
    }
    return *state_;
  }

  // グローバル変数の初期化で呼ばれるのでファイルには触らない
  Image Asset::image(AssetPath path)
  {
    const std::filesystem::path writtenPath = Internal::toPath(path.path());
    const std::filesystem::path sourceDirectory = Internal::toPath(path.location().file_name()).parent_path();
    const std::string key = Internal::toUtf8(sourceDirectory / writtenPath);

    auto& images = state().images;
    if (const auto found = images.find(key); found != images.end())
    {
      return Image(found->second);
    }

    const auto data = std::make_shared<Internal::ImageData>(this, writtenPath, sourceDirectory);
    images.emplace(key, data);
    return Image(data);
  }

  void Asset::update()
  {
    Internal::commitDraw(this);
  }

  Image::Image(const std::shared_ptr<Internal::ImageData>& data)
    : data_(data)
  {
  }

  bool Image::load() const
  {
    const std::shared_ptr<Internal::ImageData> data = data_.lock();
    if (data == nullptr)
    {
      return false;
    }
    return Internal::loadImage(*data);
  }

  void Image::show(Position position) const
  {
    const std::shared_ptr<Internal::ImageData> data = data_.lock();
    if (data == nullptr)
    {
#ifndef NDEBUG
      static bool warned = false;
      if (!warned)
      {
        warned = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Image: show() was called on an image that was not given by Asset::image() or whose Asset was destroyed");
      }
#endif
      return;
    }

    // 毎周期読み直してログが溢れないよう、自動で読むのは初回だけにする
    if (data->surface == nullptr)
    {
      if (data->loadAttempted || !Internal::loadImage(*data))
      {
        return;
      }
    }
    Internal::publishDraw({ data, position.x(), position.y() });
  }
}
