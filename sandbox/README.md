# sandbox

ライブラリの軽い動作試験を行うディレクトリです。ライブラリの機能の一部ではありません。  
`README.md`, `CMakeLists.txt`, `.gitignore`を除き、全てのファイルは追跡の対象外です。重要なファイルはここに配置しないことを推奨します。  

## 使い方

`.cpp`, `.hpp`ファイルを配置して普通にコードを書くだけです。  
自動的にリポジトリ配下の`TellerLibrary/`の内部が使用されるので、パスを通す必要はありません。  

- 直下の`.cpp`は、1ファイルごとに1つの実行ファイルになります  
  (`test.cpp`なら`test.exe`)  
- フォルダを作ると、その中の`.cpp`がまとめて1つの実行ファイルになります  
  (`testmod/`なら`testmod.exe`)  

### ビルドと実行

Microsoft公式のCMake Toolsを使います。  

1. プロジェクトルートのフォルダをVS Codeで開く
2. 画面下のステータスバーでプリセットを選ぶ
3. F7キーでビルドする
4. ステータスバーで実行する対象を選び、Shift+F5キーで実行する

### エディタが「compile_commands.jsonに見つからない」と表示したとき

エディタの補完やエラー表示は、ビルドのときに作られる`build/msvc-debug/compile_commands.json`を使います。新しく足したファイルは一度ビルドするまでここに掲載されません。一度ビルドしてください。  

## 書き方の例

```cpp
#include <Teller.hpp>

int main()
{
  Teller::Window window({ .title = "Test", .width = 640, .height = 480 });
  window.create();
}
```

通常時と同じく、読み込めるのは`TellerLibrary/src/`配下の公開ヘッダのみです。  
