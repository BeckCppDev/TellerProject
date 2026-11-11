#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// 画像とフォントで共通のパスの扱い
namespace Teller::Internal
{
  // SDL3の文字列はUTF-8
  // Windowsではstringのまま変換すると日本語のパスが化けるのでu8stringを通す
  std::filesystem::path toPath(std::string_view utf8);
  std::string toUtf8(const std::filesystem::path& path);

  // 配布先には開発時のフォルダがないので、実行ファイルの隣を優先する
  // なければパスを書いたcppのフォルダから探す
  std::string resolveAssetPath(const std::filesystem::path& writtenPath, const std::filesystem::path& sourceDirectory);
}
