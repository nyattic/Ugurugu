<p align="center">
  <img src="resources/icons/WobblePaint.png" width="128" alt="WagleWaglePaint アプリアイコン">
</p>

# WagleWaglePaint

[![Latest Release](https://img.shields.io/github/v/release/nyattic/WagleWaglePaint?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/nyattic/WagleWaglePaint/total?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases)
![License](https://img.shields.io/badge/license-GPL--3.0-ffc94a?style=for-the-badge&logo=gnu&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

<p align="center"><a href="README.md">KR</a> · <a href="README.en.md">EN</a> · <b>JP</b></p>

すべての線が揺れるネイティブドローイングアプリです。一度描くだけで、
スケッチが生きているようなボイリングラインアニメーションになり、
ループ再生されるGIFとしてすぐに書き出せます。

Shake Art DELUXEとPS1スタイルの頂点ジッターに着想を得ており、
レイヤー、タブレット筆圧、選択ツール、プロジェクトファイル、
自動アップデートに対応しています。

## ベータ版について

> [!WARNING]
> WagleWaglePaintは現在ベータ版です。バグや荒削りな部分に出会う
> ことがあります。バグ報告は大歓迎です —
> [GitHub Issue](https://github.com/nyattic/WagleWaglePaint/issues)に、
> 何をしたか、どんな結果を期待したか、実際にはどう動いたかを書いて
> ください。`.wagle`プロジェクトファイルを添付していただけると原因
> 究明にとても役立ちます。

## ダウンロード

下のファイル名をクリックすると最新バージョンがすぐにダウンロード
されます。全リストは
[GitHub Releases](https://github.com/nyattic/WagleWaglePaint/releases/latest)で
確認できます。

| プラットフォーム | 対応環境 | ダウンロード |
| --- | --- | --- |
| Windows | Windows 10以降、x64 | [WagleWaglePaint-Windows-x64-Setup.exe](https://github.com/nyattic/WagleWaglePaint/releases/latest/download/WagleWaglePaint-Windows-x64-Setup.exe) |
| macOS | macOS 14以降、Apple Silicon | [WagleWaglePaint-macOS-arm64.dmg](https://github.com/nyattic/WagleWaglePaint/releases/latest/download/WagleWaglePaint-macOS-arm64.dmg) |

リリースページの`.zip`、`.nupkg`、`appcast.xml`、`.json`ファイルは
自動アップデートに使われるものです。通常のインストールには必要
ありません。

## インストール

### Windows

ダウンロードしたSetupファイルを実行してください。WagleWaglePaintは
現在のユーザーアカウントにインストールされ、完了すると自動的に
起動します。

### macOS

DMGを開いてWagleWaglePaintをApplicationsフォルダーへドラッグして
ください。

現在のビルドは信頼された開発者証明書で署名・公証されていないため、
Windows SmartScreenやmacOS Gatekeeperが警告を表示することが
あります。アプリは必ず公式Releasesページからのみダウンロードして
ください。macOSではアプリをControlクリックして**開く**を選び、
必要なら確認ダイアログを承認してください。

## 機能

- タブレット筆圧対応のブラシと消しゴム
- ペン・マーカー・エアブラシ・スプレーの内蔵プリセット17種
- 手描きアニメーションツールに着想を得た、くっきりしたピクセル
  境界のストローク
- 滑らかな線のためのストローク別アンチエイリアスオプション
- ウォブルアニメーションをオフにして通常のペイントツールとして
  使えるモード
- フレームごとに独立して描き直される、完全にループ可能な
  ボイリングラインウォブル
- 移動・拡大縮小・回転・複製・削除・取り消しに対応したなげなわ
  選択と自動選択
- 選択範囲を認識するブラシ・消しゴム・塗りつぶし編集
- 既存の絵とブラシサイズを一緒にスケールするキャンバスサイズ変更
- サムネイル・表示切り替え・不透明度・ドラッグ並べ替えに対応した
  レイヤー
- ウォブルの強さ・フレーム数・FPSの調整
- グローバルウォブルに重ねるストローク別の粗さ調整
- ループGIF書き出しと現在フレームのPNG・JPG書き出し
- 取り消し・やり直しに対応した`.wagle`プロジェクトファイル
- 異常終了後の未保存作業の自動復元
- 最近使った色の保持とデフォルト保存フォルダーの設定
- ショートカットのカスタマイズ、韓国語・英語・日本語
  インターフェース
- macOSはSparkle、WindowsはVelopackによる自動アップデート

## 操作方法

以下はデフォルトのショートカットです。**設定 → ショートカット**で
変更できます。

| キー | 動作 |
| --- | --- |
| `B` | ブラシ |
| `E` | 消しゴム |
| `L` | なげなわ選択 |
| `W` | 自動選択 |
| `G` | 塗りつぶし |
| `P` | プレビューの再生・一時停止 |
| `M` | キャンバスを左右反転（表示のみ） |
| `Space` + ドラッグ | キャンバスを移動 |
| スクロール | ズーム |
| `Ctrl/Cmd++` / `Ctrl/Cmd+-` | 拡大 / 縮小 |
| `Ctrl/Cmd+Space` + ドラッグ | ペンやマウスでズーム（右へドラッグで拡大） |
| `Alt` + クリック | キャンバスから色を採取（ブラシ・消しゴム・塗りつぶしツール） |
| `Ctrl+Z` / `Ctrl+Y` | 取り消し / やり直し（Windows） |
| `Cmd+Z` / `Cmd+Shift+Z` | 取り消し / やり直し（macOS） |
| `Ctrl/Cmd+E` | アニメーションGIFを書き出す |
| `Ctrl/Cmd+D` | 選択範囲を複製 |
| `Ctrl/Cmd+0` | キャンバスをウィンドウに合わせる |
| `Esc` | 現在のストロークまたは選択を取り消す |

## 設定

ツールバーの歯車ボタンから設定を開けます。Windowsでは**編集 →
設定**、macOSでは**WagleWaglePaint → 設定**からも開けます。

- **一般:** インターフェース言語を選択します。言語を変更したら
  アプリを再起動してください。
- **描画:** ウォブルアニメーションのオン・オフと、描画中の
  アニメーション動作を選択します。
- **ファイル:** 保存・書き出しダイアログが使うデフォルトフォルダー
  を指定します。
- **ショートカット:** すべてのショートカットを好きなキーに変更
  でき、必要ならデフォルトに戻せます。
- **情報:** 現在インストールされているWagleWaglePaintのバージョン
  を確認します。

## 自動アップデート

WagleWaglePaintは起動後にアップデートを確認します。**ヘルプ →
アップデートを確認**でいつでも手動確認できます。アップデートは
macOSではSparkle、WindowsではVelopackを通じてダウンロード・
インストールされ、アップデートのポップアップで新バージョンの
リリースノートを確認できます。

## 開発者向け

ソースからのビルドとテスト方法は[BUILDING.md](BUILDING.md)を参照
してください。

## ライセンス

WagleWaglePaintは[GNU General Public License v3.0](LICENSE)のもとで
配布されています。

Copyright (C) 2026 Nyabi (nyattic)
