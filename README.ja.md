<p align="center">
  <img src="resources/icons/Ugurugu.png" width="128" alt="Ugurugu アプリアイコン">
</p>

# Ugurugu

[![Latest Release](https://img.shields.io/github/v/release/nyattic/Ugurugu?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/Ugurugu/releases/latest)
[![Downloads](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fnyattic%2FUgurugu%2Fdownload-badge%2Fdownloads.json&style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e)](https://github.com/nyattic/Ugurugu/releases)
![License](https://img.shields.io/badge/license-GPL--3.0-ffc94a?style=for-the-badge&logo=gnu&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

<p align="center"><a href="README.md">KR</a> · <a href="README.en.md">EN</a> · <b>JP</b></p>

絵がゆらゆら動き出すお絵かきアプリです。
難しいアニメーション制作を覚えなくても、絵を描いて再生ボタンを押す
だけで、生きているように動く作品を楽しめます。

完成した作品は、繰り返し再生されるGIFやWebP、背景が透明な画像として
保存できます。WiggleWiggleToolで作った`.wawa`の絵も引き続き編集できます。

> [!NOTE]
> 問題が起きたときは、
> [GitHub Issue](https://github.com/nyattic/Ugurugu/issues)に
> 何をしていたか、実際に何が起きたかを書いてください。可能であれば
> `.ugu`ファイルも添付していただけると、原因を見つけやすくなります。

## ダウンロードとインストール

| プラットフォーム | 対応環境 | ダウンロード |
| --- | --- | --- |
| Windows | Windows 10以降、64ビット | [Windowsインストーラー](https://github.com/nyattic/Ugurugu/releases/latest/download/Ugurugu-Windows-x64-Setup.exe) |
| macOS | macOS 14以降、Apple Silicon | [macOSインストーラー](https://github.com/nyattic/Ugurugu/releases/latest/download/Ugurugu-macOS-arm64.dmg) |

### Windows

ダウンロードしたSetupファイルを実行してください。Windowsから確認の
警告が出た場合は、**詳細情報 → 実行**を選びます。安全のため、必ず公式の
[Releasesページ](https://github.com/nyattic/Ugurugu/releases/latest)から
ダウンロードしたファイルを使用してください。

### macOS

DMGを開き、UguruguをApplicationsフォルダーへドラッグして
ください。macOS版はAppleの確認を受けてから配布されます。

リリースページにあるその他のファイルは、自動アップデートに使われます。
通常のインストールでは、上の表にあるファイルだけをダウンロードすれば
大丈夫です。

## はじめて使うとき

1. アプリを開いて新しいキャンバスを作るか、以前の絵を開きます。
2. 左側からブラシを選び、線を描きます。
3. ゆらぎボタンで動きを選び、`P`を押して再生します。
4. **ファイル**メニューから、動くGIF・WebPまたはPNG・JPG画像として
   書き出します。

`F1`を押すと、いつでもアプリ内の簡単な使い方を確認できます。

## できること

### 好きな動きの線を作る

- なめらかな動き、コマ送りのような動き、使い慣れた基本の動きから
  選べます。
- 揺れる大きさ、動きの細かさ、線が一緒に動く度合い、ランダム感を
  調整できます。
- ところどころ切れた線を作ったり、消した部分も絵と一緒に動かしたり
  できます。
- 動きをオフにすれば、普通のお絵かきアプリとしても使えます。

### 気持ちよく描いて塗る

- 筆圧に対応したブラシと消しゴム、ペン・マーカー・エアブラシ・
  スプレーなど17種類の道具が用意されています。
- 手ぶれ補正を調整して、ゆっくり描いた線や長い線を滑らかにできます。
- 塗りつぶしでは、どこまで似た色を塗るか、今のレイヤーだけを見るか
  他のレイヤーも見るかを選べます。
- 自由な形・四角形・楕円で範囲を選び、移動、拡大縮小、回転、反転、
  コピーと貼り付けができます。
- 範囲を選ぶ代わりに、囲んだ形をそのまま塗る使い方もできます。

### レイヤーと画像を扱う

- レイヤーを重ねたりグループにまとめたりして、透明度や色の重なり方を
  変えられます。
- 見た目を変えずにまとめられる場合だけ、下のレイヤーと安全に結合します。
- 写真や別の絵を新しいレイヤーに置き、移動、拡大縮小、回転ができます。
  何度サイズを変えても、元の画像をもとに表示します。
- キャンバスを切り抜いたり広げたり、絵全体の大きさを変えたりできます。

### 保存して共有する

- 作品は`.ugu`ファイルに保存され、レイヤーや動きの設定も残ります。
- 以前のバージョンで作った`.wagle`や`.wobble`のファイルもそのまま開けます。
- 動くGIF・WebP、1枚のPNG・JPG画像として書き出せます。
- 透明なキャンバスを使えば、背景のないステッカー風の画像や
  アニメーションを作れます。
- よく使うブラシ・塗り・ゆらぎ設定を`.wwpreset`ファイルに保存し、別の
  パソコンでも読み込めます。
- アプリが予期せず終了しても、次に起動したときに作業を復元できます。

### 自分に合わせる

- アプリ内で使われるアクセントカラーを好きな色に変えられます。
- すべてのショートカットを変更し、いつでも初期設定に戻せます。
- 日本語、韓国語、英語の画面を選べます。
- 新しいバージョンをアプリ内で確認して更新できます。

## WiggleWiggleToolの絵を続ける

WiggleWiggleTool 10で保存した`.wawa`ファイルを**ファイル → 開く**から
読み込めます。元のファイルは変更せず、新しい作品として開きます。最初に
保存するときは、同じ名前の`.ugu`ファイルが提案されます。

二つのアプリでは描き方が異なるため、一部の揺れ、エアブラシ、塗りつぶした
形は少し違って見えることがあります。読み込みが終わると、変更された項目や
読み込めなかった項目をアプリが知らせます。

## よく使うショートカット

以下は初期設定です。**設定 → ショートカット**ですべて変更できます。

| キー | 動作 |
| --- | --- |
| `B` / `E` | ブラシ / 消しゴム |
| `L` / `W` / `G` | 範囲選択 / 自動選択 / 塗りつぶし |
| `P` | 動きの再生または一時停止 |
| `Space` + ドラッグ | キャンバスを移動 |
| スクロール | 拡大または縮小 |
| `Alt` + クリック | 絵から色を取り出す |
| `Ctrl/Cmd+C`, `X`, `V` | コピー / 切り取り / 貼り付け |
| `Ctrl+Z` / `Ctrl+Y` | 元に戻す / やり直す（Windows） |
| `Cmd+Z` / `Cmd+Shift+Z` | 元に戻す / やり直す（macOS） |
| `Ctrl/Cmd+0` | キャンバスをウィンドウに合わせる |
| `Ctrl/Cmd+1` | 実際のピクセルサイズで表示 |
| `Enter` / `Esc` | 選択範囲の変更を適用 / キャンセル |
| `F1` | アプリ内ヘルプを開く |

## 設定とアップデート

歯車ボタンから、言語、アプリの色、描画の動作、保存先フォルダー、
ショートカットを変更できます。Windowsでは**編集 → 設定**、macOSでは
**Ugurugu → 設定**からも開けます。

アプリは起動時に新しいバージョンがあるか確認します。**ヘルプ →
アップデートを確認**から、いつでも手動で確認できます。

## 開発者向け

ソースからビルドする場合は[BUILDING.md](BUILDING.md)を参照してください。

## クレジット

- Development support by seuppi
- App icon artwork by seuppi

## ライセンス

Uguruguは[GNU General Public License v3.0](LICENSE)のもとで
配布されています。同梱のフォントとライブラリの著作権および利用条件は
[サードパーティ通知](THIRD_PARTY_NOTICES.md)で確認できます。

Copyright (C) 2026 Nyabi (nyattic)
