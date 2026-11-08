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
    std::filesystem::path sourceDirectory;

    // キーは探した結果のパス
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
    std::string resolvePath(const AssetState& state, std::string_view path)
    {
      const std::filesystem::path relative = toPath(path);

      if (const char* basePath = SDL_GetBasePath())
      {
        const std::string besideExecutable = toUtf8(toPath(basePath) / relative);
        if (SDL_GetPathInfo(besideExecutable.c_str(), nullptr))
        {
          return besideExecutable;
        }
      }
      return toUtf8(state.sourceDirectory / relative);
    }
  }
}

namespace Teller
{
  Asset::Asset(std::source_location location)
    : state_(std::make_unique<Internal::AssetState>())
  {
    state_->sourceDirectory = Internal::toPath(location.file_name()).parent_path();
  }

  Asset::~Asset()
  {
    Internal::notifySourceReleased(this);
  }

  Image Asset::loadImage(std::string_view path)
  {
    const std::string resolved = Internal::resolvePath(*state_, path);

    if (const auto found = state_->images.find(resolved); found != state_->images.end())
    {
      return Image(found->second);
    }

    SDL_Surface* surface = SDL_LoadPNG(resolved.c_str());
    if (surface == nullptr)
    {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Teller::Asset: failed to load %s: %s", resolved.c_str(), SDL_GetError());
      return Image();
    }

    const auto data = std::make_shared<Internal::ImageData>(this, surface);
    state_->images.emplace(resolved, data);
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
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Teller::Image: show() was called on an image that is not loaded");
      }
#endif
      return;
    }
    Internal::publishDraw({ data, position.x(), position.y() });
  }
}
