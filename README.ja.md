<p align="center">
  <img src="resources/icons/WobblePaint.png" width="128" alt="WagleWaglePaint アプリアイコン">
</p>

# WagleWaglePaint

[![Latest Release](https://img.shields.io/github/v/release/nyattic/WagleWaglePaint?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases/latest)
[![Downloads](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fnyattic%2FWagleWaglePaint%2Fdownload-badge%2Fdownloads.json&style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e)](https://github.com/nyattic/WagleWaglePaint/releases)
![License](https://img.shields.io/badge/license-GPL--3.0-ffc94a?style=for-the-badge&logo=gnu&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

<p align="center"><a href="README.md">KR</a> · <a href="README.en.md">EN</a> · <b>JP</b></p>

すべての線が揺れる、ゆらゆらドローイングアプリです。一度描くだけで、
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
上部のダウンロードバッジはインストーラーと実際のアップデート
パッケージだけを集計し、更新確認用メタデータのリクエストは除外します。

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
ください。macOSではControlキーを押しながらアプリをクリックして**開く**を選び、
表示された確認ダイアログでも**開く**を選んでください。

## 機能

- 筆圧対応のブラシ・消しゴムと、ペン・マーカー・エアブラシ・
  スプレーのプリセット17種
- 手が震えても線が滑らかに引ける手ぶれ補正（ブラシと消しゴムで
  それぞれ0～100%に調整）
- フレームごとに描き直されるループ可能なボイリングラインウォブル
  （オフにして通常のペイントツールとしても使えます）
- 線ごとに、輪郭をくっきりしたピクセルにするか滑らかにするか、
  粗さをどれくらいにするかを調整
- 描き終えた線を引き直さずに、色・太さ・粗さだけを後から変更
- 長い線や高解像度キャンバスでもスムーズな描画・パン・ズーム
- 移動・拡大縮小・回転・反転をプレビューしてから適用できる
  なげなわ選択と自動選択（選択内容の削除も取り消し・やり直しに対応）
- 絵を拡大縮小せず切り抜き・拡張するキャンバスサイズ変更と、
  絵ごと拡大縮小する画像サイズ変更
- サムネイル・表示切り替え・不透明度・ドラッグ並べ替えに対応した
  レイヤーと、フォルダーのようにレイヤーをまとめられるグループ
- 通常・乗算・スクリーン・オーバーレイの合成モードと、すぐ下のレイヤーで
  色がある部分にだけ重なるクリッピング
- ウォブルの強さ・フレーム数・FPSを調整できるタイムラインと
  リアルタイムプレビュー
- 1～1600%ズーム、実ピクセルの100%表示、ウィンドウに合わせる表示
- 進捗表示とキャンセルに対応したループGIFとPNG・JPG静止画の書き出し、
  `.wagle`プロジェクトファイル
- 異常終了後の自動復元と、最後に使用したツール・色・ブラシ設定の保持
- ショートカットのカスタマイズ、韓国語・英語・日本語インターフェース
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
| ステータスバーのズームスライダー | 1～1600%で拡大・縮小 |
| `Alt` + クリック | キャンバスから色を採取（ブラシ・消しゴム・塗りつぶしツール） |
| `Ctrl+Z` / `Ctrl+Y` | 取り消し / やり直し（Windows） |
| `Cmd+Z` / `Cmd+Shift+Z` | 取り消し / やり直し（macOS） |
| `Ctrl/Cmd+E` | アニメーションGIFを書き出す |
| `Ctrl/Cmd+D` | 選択範囲を複製 |
| `Ctrl/Cmd+0` | キャンバスをウィンドウに合わせる |
| `Ctrl/Cmd+1` | 実ピクセルの100%で表示 |
| `Enter` | 保留中の選択変形を適用 |
| `Esc` | 現在のストロークまたは選択変形をキャンセルし、それ以外では選択解除 |

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

WagleWaglePaintは起動時にアップデートを確認します。前回の確認から6時間
経っていなければ省略するので、1日に何度も起動しても確認が何度も走ること
はありません。**ヘルプ → アップデートを確認**でいつでも手動確認できます。アップデートは
macOSではSparkle、WindowsではVelopackを通じてダウンロード・
インストールされ、アップデートのポップアップで新バージョンの
リリースノートを確認できます。

## 開発者向け

ソースからのビルドとテスト方法は[BUILDING.md](BUILDING.md)を参照
してください。

## ライセンス

WagleWaglePaintは[GNU General Public License v3.0](LICENSE)のもとで
配布されています。同梱のPretendard JPフォントは
[SIL Open Font License 1.1](resources/fonts/OFL.txt)に従います。

Copyright (C) 2026 Nyabi (nyattic)
