# stb_truetype

.ttfと.otfを読み描画するライブラリ  

## 取り込み元

`https://github.com/nothings/stb`の`stb_truetype.h`を使ってます。  
コミットIDは`4e9b368bfc071e97206ea5c6adba68dd0cbf8897`です。  

## LICENSE

ALTERNATIVE Bとして使用しています。ライセンスの表記は義務ではありません。  

## 改造した部分

`TELLER EXTENSIONS`と書いてある箇所を改造しています。  
下記の4つの機能を追加してあります。  

- 2値の描画
- フォントに埋め込まれたドット絵の読み取り
- 太字、斜体、縁取りの加工
- 下線の位置の読み取り

## 注意

`stb_truetype`を使用している場合は、衝突を回避するため`STBTT_STATIC`を定義してください。  
