# Ugurugu 웹 이식 진행 현황

- 기준일: 2026-08-13
- 브랜치: `wasm-engine-spike`·`web-measure-recovery`·`web-port-next`·`web-shell-fixes` 전부 main에 머지됐고, 현재 웹 전용 브랜치는 없다.
- 기준 계획: [web-itchio-feasibility.md](web-itchio-feasibility.md)의 권장 3번 방향과 단계 0~6

## 1. 완료된 작업

### 단계 1 수직 기술 spike — 핵심 게이트 통과

- 툴체인: Emscripten 4.0.7(`~/emsdk`), Qt 6.11.1 `wasm_singlethread`+호스트 `macos` 킷(`~/Qt/6.11.1`, aqtinstall, qtbase만). 빌드 방법은 BUILDING.md의 WebAssembly 절 참고.
- `d11e263` — `UGURUGU_ENGINE_SOURCES`(Qt Core/Gui만으로 빌드)와 `UGURUGU_DESKTOP_SERVICE_SOURCES` 분리. 데스크톱 `ugurugu_core`는 두 목록의 합집합이라 기존 빌드 무변화(13개 테스트 스위트 전부 통과 확인).
- `7488a2f` — `wasm-release` 프리셋, `ugurugu_engine` 정적 라이브러리, `src/wasm/EngineBridge.cpp` C ABI, Node 스모크(`tools/wasm_engine_smoke.mjs`), 브라우저 Worker 하네스(`tools/wasm_worker_harness/`), 네이티브 비교 프로브(`ugurugu_engine_digest_probe`).

보고서가 "가장 중요한 미확인 가설"로 지정한 항목의 검증 결과:

| 가설 | 결과 |
|---|---|
| Qt Core/Gui Wasm을 Dedicated Worker에서 headless 구동 | 성립. QGuiApplication/QCoreApplication 인스턴스 없이 QImage/QPainter 렌더 동작, WebGL 요구 미발생 |
| 렌더 결정성 | `examples/Wave.ugu`(wobble 30프레임) 프레임 0/15/29 픽셀 sha256이 네이티브 macOS arm64와 **비트 동일**. wobble 픽스처조차 플랫폼별 baseline 불필요 |
| 직렬화 결정성 | 재직렬화 바이트가 네이티브와 **비트 동일**, 라운드트립 안정 |

- 릴리스 wasm 크기 9.4 MB(비압축, LTO/Brotli 미적용).
- 발견한 함정: Qt wasm 정적 라이브러리는 내부적으로 emscripten::val을 쓰므로 plain `add_executable` 링크에 `-lembind`가 필요하다. Qt 6.11.1의 wasm 온라인 패키지는 Windows에서 빌드된 단일 아카이브 하나뿐이며(파일명의 `Windows-…-WebAssembly`), 호스트 도구는 `QT_HOST_PATH`가 공급하므로 macOS에서 그대로 쓴다.

### 웹 셸 (단계 3 골격)

- `fb00817` — UI 프레임워크를 React에서 **Svelte 5**로 확정, 보고서 7.1 갱신.
- `2c2af5b` — `web/`에 Vite+Svelte 5+TypeScript strict 셸: `EngineClient`(id 기반 Worker RPC) ↔ `public/engine/engine-worker.js`(Wasm 엔진 소유, premultiplied BGRA→straight RGBA 변환) ↔ 캔버스 표시. 데모 문서 자동 로드, 프레임 슬라이더, 문서 fps 재생, `.ugu` 열기/Blob 다운로드.
- `0f7316f` — 드로잉. Pointer Events(`setPointerCapture`, `getCoalescedEvents`, 펜 압력) → Worker 배치 전송 → 진행 중 스트로크는 문서 COW 복사본 위 프리뷰, 떼면 `DocumentController::addStroke`로 히스토리 커밋. 실행 취소/다시 실행은 데스크톱과 같은 `DocumentUndoStack`.
- `d0d33f8` — 브러시 프리셋(`BrushPresetCatalog` 그대로), 손떨림 보정(`StrokeStabilizer`), 레이어 패널(추가/삭제/이동/이름변경/표시/불투명도, 전부 controller 경유라 undo 대상), 증분 프리뷰(`IncrementalStrokeRenderer` 타일 + `renderLayerSplit`/`composeLayerSplitRegion`, dirty rect만 변환·전송, split 불가 문서는 전체 렌더 폴백).

셸 번들 48 KB(gzip 18.6 KB). 실행: `cmake --build --preset wasm-release && cd web && npm install && npm run dev`.

- `be6c378` — IndexedDB 자동 복구 슬롯 1개: 변경이 있을 때만 15초 주기(및 탭 hidden 시)로 `.ugu` 바이트 스냅샷을 저장하고, 재접속 시 배너로 복구/삭제를 제안하며, quota 등 저장·읽기 실패는 상태 표시줄에 노출한다(`web/src/lib/RecoveryStore.ts`). 현재 프레임 PNG export 버튼(캔버스 `toBlob`, 내보내기 직전 전체 렌더로 프레임 확정). headless Chromium 자동 검증 `web/tests/browser-verify.mjs`(playwright-core devDependency): 그리기→자동 저장→재접속→복구 배너→복구 후 픽셀 수 일치, PNG 서명 확인, IndexedDB 강제 실패 시 상태 표시줄 노출까지 확인.
- `d4b2e0f` — 데스크톱 패널 동등화. 레이어 썸네일(`LayerThumbnailRenderer`를 `src/render/`로 옮겨 엔진에 편입 — frame 0·wobble off 정적 렌더, dpr 반영, 커밋 후 250ms 디바운스 갱신), 컬러 서클(`ColorWheel.svelte` — 휴 링+SV 사각, 데스크톱처럼 회색이어도 링 위치 유지), 스포이드(표시 캔버스 픽셀 샘플링, 드래그 연속 추출), 최근 색 히스토리(스트로크 커밋 시 기록·localStorage 유지, 데스크톱 `ColorHistoryGrid` 의미), 레이어 불투명도는 데스크톱 LayerDock처럼 패널 하단 고정 슬라이더.
- `817303a` — GIF 내보내기. 브리지 `ugu_export_gif`가 데스크톱 `ExportWorker`와 같은 경로로 인코딩: NativeExact 전체 프레임 렌더, drift 보정 딜레이(공용화한 `AnimationExportPolicy::frameDurations`), GIF89a. 프레임 세트를 메모리에 들고 인코딩하므로 **폭×높이×4×프레임 수가 128 MiB(1024²·30프레임 수준)를 넘는 문서는 사전 거부**하고 상태 표시줄에 사유를 표시한다. WebP는 libwebp의 wasm 빌드가 필요해 계속 제외.

### 단계 1 잔여 측정 (2026-08-07, `web-measure-recovery` 브랜치, `bbe2d6c`/`2c96537`)

측정 도구는 `ugurugu_stress_document_generator`(1024/2048 스트레스 문서를 결정적으로 생성 — 4 레이어, 2,000 스트로크(Line/Airbrush/Spray, 일부 Erase), 200,000 포인트, 파일 약 11.3 MiB)와 `tools/wasm_engine_bench.mjs`(open·렌더·스트로크 지연과 wasm heap 측정)다. 측정 환경: macOS arm64, Node 24, 링크 최적화 적용 후 빌드.

| 항목 | Wave.ugu (640×400) | 스트레스 1024² | 스트레스 2048² |
|---|---:|---:|---:|
| 문서 열기 | 13 ms | 538 ms | 502 ms |
| 첫 전체 렌더 | 6 ms | 2.8 s | 5.6 s |
| 스트로크 시작 p95 | 4 ms | 5.7 s | 11.5 s |
| 스트로크 배치(append→렌더 반영) p50/p95 | 0.1/0.3 ms | 0.2/0.4 ms | 0.2/0.4 ms |
| 스트로크 커밋 p95 | 4 ms | 2.9 s | 5.7 s |
| serialize | 1 ms | 621 ms | 632 ms |
| wasm heap 피크 | 22 MiB | 103 MiB | 150 MiB |

핵심 해석:

- 진행 중 스트로크의 증분 경로(배치 append→dirty rect 반영)는 문서 밀도와 캔버스 크기에 거의 무관하게 p95 0.4 ms 수준이다. 웹의 그리기 반응성 자체는 게이트를 통과한다.
- 측정 과정에서 라이브 프리뷰 버그를 발견·수정했다: 브리지가 `composeLayerSplitRegion`에 전체 크기 레이어 이미지를 넘겨(계약은 패치 크기 `patch.layerImage`) 합성이 조용히 null을 돌려주고 dirty가 항상 비어, **손을 뗄 때까지 선이 화면에 나타나지 않았다**. 데스크톱과 같은 patch 단위 합성 + `documentForLayer`(레이어 wobble 반영)로 고치고, 브라우저 검증에 "드래그 중 프리뷰 표시" 회귀 체크를 추가했다. null 합성은 이제 무시가 아니라 전체 프리뷰 폴백으로 처리한다.
- 반면 스트로크 시작(=`renderLayerSplit`+커밋 프레임 렌더)과 커밋(전체 재렌더)은 전체 렌더 비용에 비례한다. 2,000 스트로크 밀도에서는 1024²에서도 수 초라 다음 최적화 지점이 명확해졌다(아래 한계 참고). 이 비용은 wobble이 모든 스트로크를 프레임마다 재래스터화하는 데서 온다.
- 메모리는 걱정보다 작다. 최악 밀도의 2048² 문서도 엔진 wasm heap 피크 150 MiB로, 데스크톱 브라우저는 물론 모바일 상한(보수적으로 300~400 MiB)에도 들어온다. 파일→열린 문서 heap 증가는 약 4.4배(11.3 MiB 파일 → +50 MiB)였다.

### 릴리스 빌드 크기 (LTO/size 최적화 후)

| 산출물 | 원본 | gzip -9 | brotli -q 11 |
|---|---:|---:|---:|
| `ugurugu_engine_spike.wasm` | 6.68 MB | 2.22 MB | 1.61 MB |
| `ugurugu_engine_spike.js` | 133 KB | 34 KB | 30 KB |
| 셸 번들(js+css) | 55 KB | 21 KB | — |

- 최적화 전 wasm 9.64 MB → 6.68 MB. 두 가지가 컸다: ① CMake Release 링크 커맨드에는 `-O` 플래그가 전달되지 않아 emcc가 **-O0로 링크**(wasm-opt·JS 압축 생략)하고 있었다 — `target_link_options`에 `-O3`를 명시해야 한다. ② headless 엔진에 wasm QPA 플러그인·Qt6OpenGL·gif/ico/jpeg 이미지 플러그인이 딸려 들어오고 있어 `qt_import_plugins(… EXCLUDE_BY_TYPE platforms imageformats)`로 제외했다(스모크·digest 비교로 무해 확인). 우리 오브젝트에만 적용되는 `-flto`는 Qt 정적 라이브러리가 지배적이라 효과가 거의 없었다.
- itch.io 한도 대비: 단일 파일 200 MB의 3.3%, 전체 500 MB의 1.4% 수준. 압축 전송 기준 첫 다운로드는 2 MB 미만(brotli)이다. 크기는 게이트가 아니다.

### 웹 메모리 정책 제안 (측정 근거)

| 정책 | 데스크톱 브라우저 | 모바일 beta |
|---|---|---|
| 새 문서 상한 | 2048×2048 | 1024×1024 |
| 기본 새 문서 | 1024×768 | 1024×768 이하 |
| `.ugu` import 경고 | 32 MiB 초과 | 16 MiB 초과 |
| `.ugu` import 거부 | 64 MiB 초과 (엔진 자체 한도 128 MiB보다 낮게) | 64 MiB 초과 |
| history 예산 | 64 MiB | 32 MiB |
| 재생/미리보기 캐시 도입 시 | ≤ 64 MiB 또는 3~4프레임 | 더 낮게, 기능 감지 후 |

- 근거: 열린 문서 heap ≈ 파일 크기 × 4.4 + 렌더 표면(2048² 기준 프레임당 16 MiB) + serialize/스냅샷 시 파일 크기만큼의 일시 사본. 32 MiB 파일이면 문서만 약 140 MiB로 데스크톱은 수용, 모바일은 위험 영역이다.
- 자동 복구 스냅샷은 변경이 있을 때만 15초 주기로 저장하므로 IndexedDB 쓰기 피크는 import 상한 규칙과 같은 수치로 묶인다.
- 후속 제안: 링크 시 `-sMAXIMUM_MEMORY=512MB`를 고정해 브라우저별 기본 상한(2 GiB) 대신 명시적 실패를 조기에 받는 것을 검토한다(아직 미적용).

### 스트로크 지연 최적화 (2026-08-08, `web-port-next`)

이전 측정이 "가장 시급한 최적화 지점"으로 지목한 스트로크 시작·커밋 비용을 데스크톱 `CanvasWidget::endStroke`가 이미 쓰던 승격(promotion) 방식으로 없앴다.

- `ugu_stroke_end`는 마지막 점까지 증분 갱신한 뒤 `IncrementalStrokeRenderer::applyTo`로 스트로크를 split의 `layerBase`에 굽는다. 커밋이 `Added`로 끝나면 그 split이 곧 커밋된 문서의 split이고 `renderedFrame`은 이미 결과를 보여 주고 있으므로 **전체 재렌더를 하지 않는다**. dirty는 마지막 증분 영역뿐이다.
- 승격된 split은 다음 스트로크까지 살아남는다. 같은 레이어·같은 프레임이면 `ugu_stroke_begin`이 `renderLayerSplit`을 건너뛴다.
- split을 새로 만들어야 할 때도 커밋 프레임은 `composeLayerSplit(split, split.layerBase)`로 합성한다. `tests/LayerSplitPreviewTests.cpp:41`이 이 합성이 `render()`와 같음을 이미 고정하고 있어, 전체 렌더 2회가 1회로 줄었다.
- undo/redo와 레이어 조작은 split을 무효화한다. 승격된 split은 커밋된 문서에만 유효하기 때문이다.

| 항목 | Wave.ugu | 스트레스 1024² | 스트레스 2048² |
|---|---:|---:|---:|
| 스트로크 시작 — 최초(cold) | 3 ms | 2.7 s | 5.5 s |
| 스트로크 시작 — 이후(warm) p95 | 0.1 ms | **0.3 ms** | **0.3 ms** |
| 스트로크 커밋 p95 | 3.4 ms | **1.7 ms** | **2.2 ms** |
| (이전) 시작 p95 | 4 ms | 5.7 s | 11.5 s |
| (이전) 커밋 p95 | 4 ms | 2.9 s | 5.7 s |

커밋은 초 단위에서 밀리초 단위로, 연속 스트로크의 시작은 사실상 0이 됐다. 남은 초 단위 비용은 **문서를 열거나 undo한 직후의 첫 스트로크 한 번**뿐이고, 그 값은 첫 전체 렌더 비용과 같다(1024² 2.7 s / 2048² 5.5 s). 이는 사용자가 이미 문서를 열 때 한 번 기다리는 비용과 같은 크기이며, 더 줄이려면 wobble 프레임 캐시가 필요하다.

벤치마크(`tools/wasm_engine_bench.mjs`)는 이제 cold/warm을 나눠 보고하며, 실제 드로잉을 반영하려고 스트로크 사이에 undo를 하지 않는다.

### 단계 2 — 버전 있는 ABI와 오류 모델 (2026-08-08)

- `ugu_abi_version()`을 추가했다. 워커가 시작할 때 값을 확인하고 다르면 즉시 실패한다. 이전에는 오래된 `public/engine` 아티팩트가 관계없는 지점의 `… is not a function`으로 나타났다.
- `ugu_last_error_code()`가 구조화된 상태 코드를 돌려준다(0 ok, 1 invalid argument, 2 out of memory, 3 invalid document, 4 no paint layer, 5 stroke rejected, 6 render failed, 7 export too large, 8 export failed). 문자열은 진단용으로만 쓴다.
- CI에 `Wasm engine parity` job을 추가했다. Qt wasm 킷과 Emscripten 4.0.7을 설치해 `wasm-release`를 빌드하고, Node 스모크의 해시가 네이티브 `ugurugu_engine_digest_probe` 출력과 일치하는지 확인한다. 지금까지 수동이던 게이트가 자동이 됐다.

### 단계 2 — 새 문서와 메모리 정책 강제 (2026-08-08)

- `ugu_document_new(width, height)`와 `ugu_set_undo_limit()`을 추가했다. 이전에는 웹 셸이 기존 `.ugu`만 열 수 있어 보고서 8.4 MVP 범위의 첫 항목인 "새 문서"가 비어 있었다.
- `web/src/lib/MemoryPolicy.ts`가 아래 제안 표를 실제 코드로 옮겼다. 프로파일은 `(pointer: coarse)`와 화면 크기로 데스크톱/모바일을 나누며 `?profile=` 질의로 덮어쓸 수 있다.
  - 새 문서 다이얼로그가 상한(데스크톱 2048, 모바일 1024)으로 클램프한다. 엔진 자체 한도 4096보다 낮다.
  - `.ugu` 열기는 경고(32/16 MiB)와 거부(64 MiB) 기준을 적용하고 상태 표시줄에 사유를 남긴다.
  - undo 한도를 프로파일에서 설정한다(데스크톱 64, 모바일 32). 엔진에 바이트 단위 히스토리 예산이 없어 개수로 근사한 값이다.

### 단계 3 — 프레젠터, 확대·이동, 단축키, 지우개 프리셋 (2026-08-08)

- `web/src/lib/CanvasPresenter.ts`가 표시 계층을 둘로 나눈다. 문서 해상도 2D 캔버스(`#document-surface`)가 픽셀의 권위이고, 보이는 캔버스는 WebGL 2로 그 텍스처를 뷰 변환에 맞춰 그린다. WebGL 2가 없거나 컨텍스트를 잃으면 Canvas 2D로 자동 폴백한다. dirty 영역만 `texSubImage2D`로 올린다.
  - 스포이드와 PNG 내보내기가 문서 표면을 읽으므로 확대·이동과 무관하게 정확하다. 이전 구조에서는 표시 캔버스를 읽고 있어 확대를 도입하는 순간 깨질 코드였다.
- `web/src/lib/ViewTransform.ts` — 커서 고정 확대, 이동, 화면 맞춤. 휠 이동, Ctrl/Cmd+휠 확대, 스페이스·가운데 버튼 드래그 이동, 두 손가락 핀치 확대·이동·회전을 지원한다. 두 번째 손가락이 닿으면 진행 중 스트로크는 커밋된다.
- `web/src/lib/Shortcuts.ts` — undo/redo, 저장, 열기, 새 문서, 도구 전환(B/E/I), 굵기(`[`/`]`), 확대(Ctrl/Cmd +/−/0/1), 프레임 이동(←/→), 재생(Enter). 브라우저·OS가 소유한 조합은 건드리지 않고, 모든 단축키에 대응하는 화면 컨트롤을 남겼다.
- 화면 맞춤은 100%를 넘지 않는다. 데스크톱도 웹도 네이티브 해상도 위로는 렌더하지 않으므로(`PreviewRenderPolicy::renderSize`가 스케일을 1.0으로 clamp한다) 작은 문서를 창에 맞춰 키우면 원본 픽셀 격자만 커진다. 축소는 화면 맞춤의 목적 그대로 두고, 확대는 명시적 조작으로만 일어난다.
- 확대 시 표시 필터는 LINEAR다. 데스크톱은 QRhi 샘플러가 Nearest이고 `renderAtSize`의 업스케일도 `Qt::FastTransformation`이라 100% 위에서 각지는데, 웹은 여기서 의도적으로 데스크톱보다 부드럽게 간다.
- 그리는 동안 재생은 꺼지지 않고 프레임 전진만 멈춘다(`CanvasWidget::advanceFrame`과 같음). 손을 떼면 우글거림이 저절로 이어진다. 데스크톱의 `canvas/animateWhileDrawing` 설정에 대응하는 토글을 재생 옆에 뒀다.
- 웹 셸 UI 문구는 전부 영어다. 데스크톱은 ko/en/ja 번역을 갖지만 웹은 아직 번역 계층이 없다.
- 지우개가 `EraserPresetCatalog`에 연결됐다(`ugu_eraser_preset_*`). 지우개를 고르면 프리셋 선택이 지우개 카탈로그로 바뀐다.
- 캔버스 밖으로 나간 스트로크 점을 브리지가 클램프한다. `isValidInputStrokePoint`는 모든 점이 캔버스 안일 것을 요구하므로, 하나라도 벗어나면 `addStroke`가 스트로크 전체를 거부해 선이 통째로 사라지고 있었다. 데스크톱이 begin·continue·end에서 `clampedDocumentPosition`으로 하는 것과 같은 계약이다. 거부된 스트로크는 이제 조용히 버려지지 않고 상태 표시줄에 노출된다.
- 브러시 안티앨리어싱 토글(`ugu_set_brush_antialiasing`). `BrushSettings::antialiasing`은 기본값이 false이고 어떤 프리셋도 이 값을 설정하지 않는다. 데스크톱은 브러시 팝오버 토글에서 스트로크마다 실어 보내는데(`CanvasWidgetTools.cpp:83`) 웹에는 그 경로가 없어, 웹에서 그린 모든 선이 앨리어싱된 채로 커밋되고 있었다. 계단뿐 아니라 우글거리는 선분 이음매에 1px 틈이 보이던 것도 같은 원인이다. 데스크톱과 같이 기본은 꺼짐이고 localStorage에 유지된다.
- itch.io 배포를 위해 Vite `base: "./"`와 상대 경로 Worker·에셋 URL로 바꾸고, `tools/check_itchio_package.mjs`가 진입 파일·절대 경로·파일 수·경로 길이·크기·대소문자 충돌을 검사한다. CI의 웹 job이 이 검사를 실행한다.

### 단계 3 — 선택·채우기 도구와 좌측 툴 레일 (2026-08-08)

보고서 8.4의 "채우기/선택 도구"가 닫혔다. 엔진 쪽 재료(`FloodFillMask`, `FrozenFillMask`, `SelectionOperation`, `StrokeMask`)는 이미 `UGURUGU_ENGINE_SOURCES`에 들어 있었으므로 새로 만든 것은 브리지와 셸뿐이다.

- 마스크 경계 추적기를 `src/ui/CanvasViewport.cpp`에서 `src/document/SelectionOutline.{hpp,cpp}`로 옮겨 엔진에 편입했다. 데스크톱 `outlinePath`는 이 결과로 `QPainterPath`를 만드는 얇은 래퍼가 됐다. `LayerThumbnailRenderer`를 옮겼을 때와 같은 이유다.
- ABI를 3으로 올리고 다음을 추가했다(현재 ABI는 4 — 아래 "공개 운영 전 결함 수정"의 `ugu_layer_id`). 페인트통 `ugu_bucket_fill`, 올가미/사각/타원 `ugu_selection_shape`(데스크톱 LassoMode의 Paint 모드 포함), 마술봉 `ugu_selection_flood`, `ugu_selection_all`/`_invert`/`_clear`/`_fill`/`_delete`, 윤곽 읽기 `ugu_selection_outline{,_size}`, 옵션 `ugu_set_fill_options`. 상태 코드에 9(선택 없음), 10(빈 영역), 11(레이어에 그릴 수 없음)이 늘었다.
- 선택은 브리지가 캔버스 크기 Grayscale8 마스크와 소속 레이어로 들고 있으며, `ugu_stroke_begin`이 그 마스크를 스트로크의 `clipMask`로 실어 보낸다. `CanvasWidget::beginStroke`와 같은 계약이라 증분 프리뷰와 커밋 렌더가 모두 선택 경계에서 잘린다. 페인트통과 선택 영역 채우기도 같은 마스크로 클립한다.
- 윤곽은 닫힌 컨투어를 `[정점 수, x, y, …]`로 이어 붙인 float 버퍼로 넘긴다. 워커가 `selectionRevision`을 비교해 **바뀐 경우에만** 읽어 보내므로 스트로크 중 매 응답마다 마스크를 훑지 않는다. 순수 선택 변경은 픽셀을 옮기지 않으므로 이미지 데이터 없는 응답으로 답한다.
- UI를 다시 배치했다. 도구가 여섯 개로 늘어 상단 바가 감당하지 못하므로 **좌측 세로 레일**(아이콘은 데스크톱 `src/ui/Icons.cpp` 글리프를 SVG로 옮긴 것)과 그 옆의 도구별 옵션 열로 나눴다. 팔레트는 데스크톱 `Theme.cpp` 값(그래파이트 + 앰버 `#FFC94A`)을 그대로 쓴다. 활성 도구 테두리는 프레임마다 다시 그려지는 손그림 윤곽이며 `prefers-reduced-motion`에서는 첫 프레임으로 고정된다.
- 마칭 앤츠는 표시 캔버스 위 오버레이 캔버스가 그리고, 선택이 없으면 애니메이션 루프가 돌지 않는다. 드래그 중인 올가미 경로도 같은 오버레이에 그린다.
- 단축키는 데스크톱 레일과 같다. B/E/L/W/G/I, Ctrl+A 전체 선택, Ctrl+Shift+I 반전, Ctrl+D·Esc 해제, Delete 삭제, Alt+Delete 채우기.

알려진 차이: 웹의 선택 상태는 셸이 들고 있어 **실행 취소 대상이 아니다**. 데스크톱은 `pushSelectionStateCommand`로 선택 전환까지 히스토리에 넣는다. (선택 영역 이동·변형은 2026-08-14에 들어왔다 — 아래 단계 6 참고.)

### 단계 4 — itch.io 스테이징 첫 업로드 (2026-08-09)

- 비공개(Draft) 스테이징 프로젝트 `nyattic/ugurugutest`에 butler로 첫 업로드를 마쳤다: `npm run build` → `check_itchio_package.mjs` → `butler push web/dist nyattic/ugurugutest:web --userversion <커밋>`. butler는 공식 broth 채널의 darwin-arm64 바이너리를 쓴다(Homebrew의 `butler`·`gitbutler`는 전부 다른 프로그램).
- butler가 만든 업로드는 **"This file will be played in the browser" 플래그가 꺼진 채로 들어온다.** 편집 페이지에서 최초 1회 켜야 하며, 안 켜면 "haven't configured how your project is embedded" 오류로 실행이 거부된다. 이후 푸시에는 유지된다.
- macOS 데스크톱 브라우저 실측 결과: Worker 부팅, wasm 로드, 새 문서 생성, WebGL 2 프레젠터, 데스크톱 메모리 프로파일 감지, 그리기까지 정상. SharedArrayBuffer 없이 동작하는 설계가 실환경에서 확인됐다.
- **Embed in page(1280×720)는 현재 레이아웃과 맞지 않는다.** 좌우 패널이 고정 폭이라 캔버스 기둥이 좁아져 1024×768 문서가 9%로 축소된다. `Click to launch in fullscreen` + Fullscreen button 조합으로 전환해 해결했다. 페이지 내 임베드를 살리려면 단계 3 잔여의 반응형 레이아웃이 전제다.
- 웹 셸은 이제 시작 시 데모 문서 대신 메모리 프로파일 기본 크기의 빈 문서를 만든다. 브리지의 브러시 안티앨리어싱 토글이 프리셋·도구 전환에 지워지던 버그도 수정했다(스트로크 시작 시 Paint 모드에만 적용, 데스크톱과 동일 계약).

### 웹 셸 결함 수정 (2026-08-09, `web-shell-fixes`)

코드 정독과 헤드리스 Chromium 실측으로 찾은 결함을 한 묶음으로 고쳤다. 전부 `web/tests/browser-verify.mjs`에 회귀 체크가 붙어 있다(48 → 75개 체크).

- **반투명 픽셀이 화면에서 검게 나오던 문제.** `CanvasPresenter`가 `premultipliedAlpha: false` 컨텍스트에 `SRC_ALPHA` 블렌딩을 켜고 있어, 프레임버퍼에 색은 프리멀티플라이되고 알파는 제곱된 채 남았다. 투명 배경 문서에 불투명도 50% 레이어를 올리면 문서 서피스의 `(255,0,0,127)`이 화면에서는 `(56,26,30)`으로 보였다(정답 `(145,19,22)`). 텍스처가 이미 straight RGBA이므로 블렌딩을 끄는 것이 맞다. PNG/GIF는 문서 서피스를 인코딩하므로 원래부터 정확했고, 그래서 **보이는 것과 저장되는 것이 달랐다**. 기본 새 문서는 배경이 불투명 흰색이라 알파가 255뿐이어서 지금까지 드러나지 않았다.
- **깨진 `.ugu`를 열면 작업 중이던 문서가 사라지던 문제.** 워커가 새 문서를 열기 전에 기존 핸들을 먼저 닫고 있었다. 이제 교체본을 먼저 만들고 성공했을 때만 이전 문서를 놓는다. 파싱 동안 문서 두 개가 살아 있지만 그 상한은 `MemoryPolicy`의 import 한도가 이미 잡는다. 셸도 실패 시 `meta`를 비우지 않는다 — 이전에는 캔버스에 그림이 보이는 채로 저장도 그리기도 안 되는 죽은 상태가 됐다.
- **재생에 back-pressure가 없던 문제.** 타이머가 직전 렌더의 완료 여부와 무관하게 매 틱 렌더를 큐에 넣어, 렌더가 프레임 간격보다 느린 문서에서는 큐가 무한히 자랐다. 워커 응답을 250 ms 지연시킨 재현에서 5초 재생이 정지 후 **29.4초**의 잔여 작업을 남겼다(수정 후 0.23초). 큐는 그리기·실행 취소와 공유하므로 셸 전체가 밀렸다.
- **엔진을 못 받으면 영원히 멈추던 문제.** `EngineClient`에 워커 `onerror`/`onmessageerror`가 없어, 워커가 `onmessage`를 설치하기 전에 죽으면 모든 요청이 영구 pending으로 남았다. 엔진 아티팩트를 404로 막으면 상태 표시줄이 "Creating a 1024×768 document…"에서 12초 뒤에도 그대로였다.
- **숨긴 레이어에 그려지던 문제.** `ugu_stroke_begin`에 `CanvasWidget::beginStroke`의 visible·opacity 가드가 없어, 보이지 않는 레이어에 아무 안내 없이 스트로크가 커밋됐다(다시 켜면 나타났다). 이제 상태 코드 11로 거부한다.
- **WebGL 컨텍스트 손실 폴백이 원리상 동작하지 않던 문제.** 캔버스는 처음 받은 컨텍스트 종류를 평생 유지하므로 같은 캔버스에 `getContext("2d")`를 다시 요청하면 null이다. 이제 표시 캔버스 위에 포인터 이벤트를 통과시키는 2D 캔버스를 새로 깔고 그쪽에 그린다. 전환은 단방향이고 상태 표시줄도 함께 바뀐다. WebGL2 컨텍스트를 얻고도 셰이더 링크에 실패하는 경로에도 같은 구멍이 있었다.
- 브러시와 지우개가 굵기를 공유하던 것을 데스크톱의 `m_brushWidth`/`m_eraserWidth`처럼 분리했고, 슬라이더 상한을 데스크톱과 같은 128로 올렸다(프리셋 기본값 72가 표현되지 않아 라벨과 슬라이더가 어긋났다).
- 함께 고친 것: 선택이 없을 때 Delete가 엔진 오류를 상태 표시줄에 띄우던 것, 창 포커스를 잃으면 스페이스 팬이 고착되던 것, 다운로드 직후 object URL을 즉시 해제하던 것. 문서 교체(열기·새 문서)는 이제 다른 요청과 같은 큐를 지나므로 오래된 작업이 새 문서에 적용될 수 없다.

CI의 `Wasm engine parity` job이 이제 셸을 빌드해 브라우저 스위트까지 돌린다. 실제 엔진이 있는 job이 거기뿐이라, 그동안 이 스위트는 수동 실행에만 의존하고 있었다.

### 공개 운영 전 결함 수정 (2026-08-10)

감사에서 "웹을 공개 운영한다면 즉시" 항목으로 지목된 셋을 고쳤다. 셋 다 브라우저 스위트에 회귀 체크가 붙어 있고(75 → 80개 체크), 수정을 되돌리면 각각 실패하는 것을 확인했다.

- **빠른 연속 입력에서 스트로크가 섞이거나 잘리던 문제.** `flushPendingPoints`가 포인트 버퍼를 *큐에 넣을 때*가 아니라 *큐가 실행될 때* 읽고 있었다. 엔진이 답하기 전에 두 번째 스트로크가 시작되면 `onPointerDown`이 버퍼를 먼저 비워, 앞 선의 꼬리가 사라지거나 — 더 나쁘게는 — 두 번째 스트로크의 점들이 아직 열려 있는 첫 스트로크에 붙었다. 이제 포인터 이벤트가 만든 그 자리에서 배치를 가져가므로, 큐 순서가 곧 스트로크 소속이 된다. 재현: 워커 메시지를 400 ms 지연시키고 좌·우에 짧은 선 두 개를 연달아 그으면 잉크 기둥이 두 덩이가 아니라 하나로 이어졌다.
- **레이어 삭제·이동 연타가 다른 레이어에 적용되던 문제.** 레이어 명령이 클릭 당시의 행 인덱스를 그대로 들고 큐를 지났다. 앞선 삭제가 끝나면 뒤 행이 전부 한 칸 당겨지므로, 두 번째 삭제는 사용자가 가리킨 적 없는 레이어를 지웠다(3 → 1). ABI를 4로 올려 `ugu_layer_id`를 추가하고, 셸은 명령이 실제로 실행되는 시점에 id로 행을 다시 찾는다. 사라진 레이어면 아무 일도 하지 않고 상태 표시줄에만 남긴다.
- **모바일에서 캔버스 폭이 0이 되던 문제.** 툴 레일(4rem)·옵션 열(13.5rem)·사이드 패널(15rem)만으로 이미 휴대폰 화면보다 넓은데, flex가 그 차이를 basis 0인 캔버스 열에서만 빼갔다. 390 px 뷰포트에서 표시 캔버스는 실제로 0×729였다. 고정 열에 `flex: none`, 캔버스 열에 `min-inline-size: 12rem` 바닥을 두고, 48rem 미만에서는 캔버스를 맨 위 전폭으로 두고 패널을 그 아래로 쌓는다. 전체 반응형 레이아웃은 여전히 남은 작업이지만, "그릴 수 없는 셸"은 이걸로 닫힌다.
- 함께: 뷰포트 면적이 0일 때(숨은 탭, 접힌 iframe) `resizeDisplay`와 화면 맞춤이 1 px 바닥으로 클램프하지 않고 그대로 둔다.

엔진 쪽 같은 묶음으로 고친 것(데스크톱과 공유):

- `ugu_stroke_begin`과 페인트통이 **숨은 그룹 안 레이어**를 거부한다. 레이어 자신의 `visible`만 보던 가드가 조상 그룹을 보지 않아, 아무도 볼 수 없는 곳에 커밋되고 있었다.
- 마술봉·페인트통의 "이 레이어" 참조 이미지가 그룹 안 레이어에서 아예 렌더되지 않던 것(합성 계획이 없는 부모를 보고 프레임 전체를 거부). 공용 `DocumentOperations::isolatedLayerDocument`로 그룹·클리핑·블렌드를 떼고 렌더한다.

### 단계 3 — 웹 캔버스 회전 (2026-08-13)

- `ViewState`에 `[-180°, 180°)`로 정규화되는 회전축을 추가했다. 화면 맞춤은 회전한 문서의 경계 상자를 사용하고, 확대·이동·화면↔문서 좌표 변환은 모두 같은 가역 변환을 공유한다.
- WebGL 2 프레젠터는 축 정렬 사각형 대신 원점과 두 축 벡터로 문서 quad를 그린다. Canvas 2D 폴백도 같은 affine transform을 사용하며, 선택 마칭 앤츠와 사각형·타원 드래그 윤곽도 문서 공간에서 함께 회전한다.
- 데스크톱과 같은 `Shift+Space` 드래그 자유 회전(0.5°/px), `Shift`+휠과 `-`/`^` 5° 회전, 좌·우·각도 입력·0° 초기화 화면 컨트롤을 추가했다. `Ctrl/Cmd+-` 확대와 충돌하지 않으며 회전은 문서·undo·내보내기를 바꾸지 않는 뷰 상태다.
- 두 손가락 제스처는 이전 중심·거리만 일부 사용하던 경로를 중심·거리·각도의 단일 변환으로 바꿨다. 이 과정에서 거리가 같으면 두 손가락 이동이 실제로 적용되지 않던 기존 결함도 고쳤다. 이전 터치 중심 아래의 문서 점이 이동·핀치·비틀기 뒤 새 중심에 그대로 고정된다.

### 단계 6 — 선택 영역 변형과 라이선스 고지 (2026-08-14)

잔여 목록에서 "코드로 닫을 수 있는" 두 항목을 닫았다. ABI는 4 → **5**.

**떠 있는(floating) 선택 변형.** 엔진 재료(`SelectionOperation`, `DocumentController::transformSelection`)는 이미 wasm에 링크돼 있었고 없던 것은 브리지와 셸뿐이었다. 데스크톱 `FloatingTransformSession`을 그대로 옮겼다.

- 브리지 `ugu_selection_transform_{begin,update,apply,cancel,active}`. begin이 `makePixelSelectionOp(mask, identity, clearSource, drawDestination)`로 픽셀을 들어 올리고, update는 행렬만 바꿔 `isValidPixelSelectionOp`로 검증한 뒤 미리보기를 그리며, apply가 `transformSelection`으로 **한 번의 실행 취소 항목**을 남긴다. 커밋 뒤에는 데스크톱 `transformSelectionOverlay`와 같은 `transformedSelectionSupport` 마스크를 설치해 마칭 앤츠가 픽셀을 따라간다.
- 미리보기는 전체 렌더가 아니라 `replayPixelSelectionOnLayerRegion` + `composeLayerSplitRegion`으로 **움직인 영역만** 패치한다(데스크톱 `CanvasWidgetPreview`와 같은 경로). split은 스트로크 커밋이 승격해 둔 것을 재사용하므로 드래그 중 비용은 이동 영역에 비례한다. split을 못 쓰는 레이어(그룹·클리핑)는 레이어 전체 → 문서 전체 순으로 폴백한다.
- 세션 중 문서가 바뀌면(undo·레이어 편집 등) apply는 커밋하지 않고 거부한다. begin 시점의 `undoStack()->index()`를 들고 있다가 비교한다. 셸도 undo/redo·레이어·프레임 이동·선택 변경·내보내기·재생 앞에서 세션을 취소한다(데스크톱 `cancelSelectionTransformForBoundary`와 같은 지점).
- 셸: 선택이 있으면 뷰포트 위에 변형 바가 뜬다. Move 토글(단축키 `M`, 데스크톱처럼 도구가 아니라 **모드**)로 선택 안쪽을 드래그해 이동, Scale(%)·Rotate(°)는 데스크톱 `MainWindow::scaleSelection`/`rotateSelection`과 같은 기본값·범위(125%, 10~400 / 90°, ±360), 좌우·상하 뒤집기. 전부 같은 행렬에 누적되고 Enter로 적용, Esc로 취소한다.
- 드래그는 포인터가 행렬을 엔진보다 빠르게 만들어 내므로 미리보기 요청을 **최신 것으로 합친다**(재생 back-pressure와 같은 이유). 앤츠는 셸이 이미 들고 있는 윤곽을 행렬로 매핑해 즉시 따라가고, 픽셀만 엔진 응답을 기다린다.
- 함정: 행렬을 `$state`에 두면 Svelte가 프록시로 넘겨 `postMessage`가 `DataCloneError`로 죽는다. `$state.raw`여야 한다.

**라이선스 고지.** 웹 빌드는 Qt를 **정적 링크**하는데 셸에 고지 UI가 아예 없었다.

- 상단 바의 About 버튼이 고지 패널을 연다. GPL-3.0-or-later, 전체 소스 위치(<https://github.com/nyattic/Ugurugu>), 이 빌드의 엔진 ABI·스키마 버전, 그리고 Qt 6.11.1(LGPL-3.0, 정적 링크와 재링크 경로)·Pretendard JP(OFL 1.1)·Svelte 5(MIT)·Emscripten 4.0.7(MIT/NCSA)을 적는다.
- 라이선스 원문은 링크만 걸지 않고 **패키지에 함께 올린다**. `sync-engine.mjs`가 `resources/licenses/`와 저장소 루트에서 `public/licenses/`로 복사하고, 브라우저 스위트가 패널이 가리키는 6개 파일을 실제로 받아 본다. Emscripten·Svelte 원문을 `resources/licenses/`에 추가한 것도 이 때문이다 — CI에는 emsdk가 없으므로 툴체인 디렉터리에서 가져올 수 없다.
- `THIRD_PARTY_NOTICES.md`에 웹 절을 넣고, 데스크톱 Qt 절의 "동적 라이브러리라 교체 가능" 문장이 웹에도 적용되는 것처럼 읽히던 것을 갈랐다.

남은 것은 **선택 전환 자체의 실행 취소**다(`pushSelectionStateCommand`/`selectionHistoryStateRequested`). 변형과 한 항목으로 묶여 있었지만 별개의 작업이라 열어 뒀다 — 아래 잔여 목록 참고.

### 검증 방법 (반복 실행 가능)

- Node 스모크: `node tools/wasm_engine_smoke.mjs [문서.ugu]` — load/render/round-trip과 해시 출력. 인자를 생략하면 `examples/Wave.ugu`.
- 네이티브 비교: `cmake --build --preset macos-debug --target ugurugu_engine_digest_probe && ./out/build/macos-debug/ugurugu_engine_digest_probe examples/Wave.ugu` — 스모크와 같은 형식의 해시.
- 측정: `cmake --build --preset macos-debug --target ugurugu_stress_document_generator`로 스트레스 문서를 만들고 `node tools/wasm_engine_bench.mjs <문서.ugu>…`로 지연·heap을 측정.
- 브라우저: `cd web && npm run build && npm run test:browser` — headless Chromium(`/Applications/Chromium.app`)으로 복구 루프(그리기→자동 저장→재접속→복구→픽셀 일치), 드래그 중 라이브 프리뷰, 레이어 썸네일 표시·갱신, 컬러 서클/최근 색/스포이드, PNG·GIF 다운로드 서명, IndexedDB 실패 노출, 확대·축소와 확대 시 문서 픽셀 불변, B/E/I·Ctrl+Z 단축키, 지우개 프리셋, 새 문서 생성과 상한 클램프, 스트로크 후 재생 유지, 안티앨리어싱 토글, 캔버스 밖으로 나간 스트로크 커밋, 그리고 올가미 선택→마칭 앤츠 표시→선택 영역 채우기(140×100 = 14,000 px 정확히), 선택 경계에서 멈추는 스트로크(오른쪽 끝 x=439), 선택 영역 삭제, 빈 레이어 전체를 채우는 페인트통(307,200 px)과 그 실행 취소, 올가미 Paint 모드, 마술봉, L/W/G/B 단축키를 자동 검증한다. 캔버스 회전은 좌·우·초기화 컨트롤, `-`/`^`와 확대 단축키 충돌, `Shift`+휠, `Shift+Space` 자유 회전, 회전된 WebGL quad와 Canvas 2D 폴백, 역좌표 입력, 회전 상태의 화면 맞춤, 두 손가락 이동·핀치·비틀기 결합과 중심 앵커를 검증한다. 여기에 실패 경로 회귀가 붙어 있다: 깨진 파일 열기가 문서를 지키는지, 숨긴 레이어가 스트로크를 거부하는지, 반투명 레이어가 화면에 제 불투명도로 나오는지(스크린샷 픽셀 판독), 컨텍스트 손실이 소프트웨어 폴백으로 넘어가는지, 느린 엔진에서 재생 정지가 잔여 큐를 남기지 않는지, 엔진 아티팩트가 없을 때 오류가 뜨는지, 느린 엔진에서 연달아 그은 두 스트로크가 두 덩이로 남는지, 삭제 연타가 레이어를 하나만 지우는지, 390×844 뷰포트에서 캔버스가 폭을 갖고 스트로크를 받는지. 선택 변형은 미리보기·적용·취소와 **한 번의 undo로 전체 이동이 되돌아가는지**, 90° 회전이 상자를 전치하는지, 150% 확대가 픽셀을 늘리는지, 선택 밖 드래그가 아무것도 옮기지 않는지를 검증하고, 고지 패널은 문구·소스 링크와 함께 **패키지가 실제로 라이선스 원문 6개를 서빙하는지**까지 받아 본다(137개 체크).
- itch.io 패키징: `cd web && npm run build && node ../tools/check_itchio_package.mjs dist`.

`89ca407` — 브라우저 스위트의 "재생 중 그린 스트로크가 커밋된다" 체크가 CI에서 간헐 실패했다. 이 체크만 커밋을 기다리지 않고 즉시 픽셀 수를 읽고 있었는데, 커밋은 재생 렌더와 큐를 공유하므로 손을 뗀 뒤 몇 프레임 지나 반영된다. 다른 체크와 같은 `waitForFunction` 폴링으로 바꾸고, 직전 스트로크와 겹치지 않는 경로로 그어 증가폭이 wobble 시드 차이가 아니라 선 하나가 되게 했다. 당시 체크 수는 80개 그대로였다.

## 2. 남은 작업

계정·실기기·법률 검토가 필요한 항목이 남은 작업의 대부분이다. 코드로 닫을 수 있는 항목은 도구·기능 추가 쪽에 몰려 있다.

2026-08-14 기준 확인: 캔버스 회전(08-13), 선택 영역 변형과 라이선스 고지(08-14)가 닫혔다. 코드로 확인한 나머지 상태 — 번역 계층 없음, `ugu_abi_version()`은 5, `@media` 블록은 48rem 하나, `LayerPanel.svelte`·`ColorWheel.svelte`의 `aria-*`는 0개, `showSaveFilePicker` 없음, GIF 진행률·취소 없음, `UguruguWasm.cmake`에는 `-sALLOW_MEMORY_GROWTH=1`만 있고 `-sMAXIMUM_MEMORY`는 없다.

### 실기기·계정·법률 (코드로 닫을 수 없음)

- iOS Safari, Android Chrome 실제 장치 스모크. 단계 1의 중단 기준 판단 재료이자 Mobile Friendly 표시의 전제다.
- itch.io 스테이징 업로드와 전체 화면 실행은 확인됐다(위 단계 4 항목). 남은 실측: iframe 안 키보드 단축키, 새로고침 후 IndexedDB 복구 유지, PNG/GIF 다운로드 권한, clipboard, 그리고 데스크톱 브라우저별 차이.
- 데스크톱 4종 브라우저 행렬, visibility 시험. 컨텍스트 손실은 `WEBGL_lose_context`로 실제 손실을 일으켜 소프트웨어 폴백을 자동 검증하지만, 드라이버가 일으키는 진짜 손실은 아직 겪어 보지 못했다.
- Qt 정적 배포 의무의 **법률 검토** (출시 전 필수). 고지 UI와 문서는 2026-08-14에 만들었다 — 셸의 About 패널이 GPL-3.0-or-later·전체 소스 위치·Qt LGPL-3.0 정적 링크와 재링크 경로를 밝히고, 라이선스 원문 6개가 패키지에 함께 올라가며, `THIRD_PARTY_NOTICES.md`에 웹 절이 생겼다. 남은 것은 이 구성이 실제로 의무를 충족하는지에 대한 판단이며, 그건 코드가 답할 수 있는 문제가 아니다.
- 메모리 정책 수치를 실기기 결과로 보정. 현재 값은 측정 기반 제안치를 그대로 코드에 옮긴 것이다.

### 단계 3 잔여 (웹 UI)

- 웹 UI 번역 계층 (현재 영어 고정, 데스크톱은 ko/en/ja)
- **선택 전환 자체의 실행 취소.** 이동·확대·회전은 2026-08-14에 닫혔지만(위 단계 6), 선택을 바꾸는 행위 자체는 여전히 히스토리 밖이다. 데스크톱은 `pushSelectionStateCommand`로 전후 마스크를 커맨드에 담고 undo 때 `selectionHistoryStateRequested`로 되돌린다. 브리지가 할 일은 그 신호를 받아 `installSelection`을 부르고, 순수 선택 변경 지점(`ugu_selection_shape`의 Select 모드, `_flood`, `_all`, `_invert`, `_clear`)에서 커맨드를 밀어 넣는 것이다. 주의할 점은 `_delete`·`_fill`·변형처럼 이미 문서 커맨드를 미는 경로에 겹쳐 밀어 실행 취소 항목이 둘이 되지 않게 하는 것.
- 모바일 반응형 레이아웃. 핀치·이동 제스처가 있고 48rem 미만에서 캔버스를 전폭으로 올려 쌓기는 하지만, 패널 자체는 데스크톱 치수 그대로다.
- 접근성 마무리. 캔버스·슬라이더 레이블과 전 기능 단축키는 넣었지만, 스크린 리더 낭독 순서와 레이어 트리의 키보드 전용 조작은 남아 있다.

### 데스크톱이 먼저 간 기능 (2026-08-13 시점 격차)

데스크톱에는 있고 웹에는 대응이 없는 기능이다. 위 잔여 목록에 없던 항목이라 여기 적는다.

- 우글 텍스트 도구 (`5cba14a`, 08-08). 텍스트를 스트로크로 굽는 `src/document/TextStrokeBuilder.{hpp,cpp}`는 `UGURUGU_ENGINE_SOURCES`에 있으므로 wasm에도 이미 링크돼 있다. 브리지·셸만 없다.
- 브러시 프리셋 버튼·도구 컨트롤 개선 (`6827f5e`, 08-11). 웹 `ToolOptions.svelte`는 이전 형태 그대로다.

참고로 두 손가락 제스처(`82578cc`)는 반대 방향이다. 웹이 먼저 가지고 있던 것을 데스크톱이 따라왔다.

### 단계 4·6 잔여

- File System Access 지원 브라우저의 "같은 파일에 다시 저장" (선택 기능)
- GIF 내보내기 진행률·취소 (현재는 Worker 블로킹)
- raster/Wawa import, 고급 selection과 layer group 편집
- 링크 시 `-sMAXIMUM_MEMORY=512MB` 고정 검토 (아직 미적용)

## 3. 알려진 한계

- 문서를 연 직후나 undo 직후의 **첫 스트로크 한 번**은 여전히 전체 렌더 1회를 쓴다(2,000 스트로크 밀도에서 1024² 2.7 s, 2048² 5.5 s). 이후 스트로크는 승격된 split을 재사용해 시작 p95 0.3 ms, 커밋 p95 2 ms다. 첫 스트로크까지 없애려면 wobble 프레임 캐시가 필요하다.
- 자동 복구 스냅샷은 스트로크 진행 중에는 건너뛰고 다음 주기에 저장한다. serialize가 수백 ms인 대형 문서에서는 스냅샷 주기 동안 Worker가 그 시간만큼 다른 명령을 받지 못한다.
- GIF 내보내기는 인코딩이 끝날 때까지 Worker를 블로킹한다(프레임 수 × 전체 렌더 + 인코딩). Wave 규모는 1초 미만이지만 스트로크 밀도가 높은 문서는 분 단위가 될 수 있다. 진행률·취소가 필요해지면 프레임 단위 분할이 다음 단계다.
- `npm run dev`는 시작할 때만 엔진 아티팩트를 복사한다. dev 서버를 띄운 채 wasm을 다시 빌드하면 `npm run sync-engine`을 다시 실행해야 브라우저가 새 엔진을 받는다. ABI 버전이 어긋나면 이제 워커가 그 사실을 명시한 오류를 던지지만, 같은 ABI 안의 동작 변경은 여전히 조용히 낡은 채로 남는다.
- 웹 엔진 아티팩트(`ugurugu_engine_spike.{js,wasm}`)는 저장소에 커밋하지 않으며 `npm run sync-engine`이 `out/build/wasm-release`에서 복사한다.
- 문서 표면과 WebGL 텍스처를 각각 하나씩 들고 있으므로 표시 계층 메모리는 문서 크기의 2배다(2048²에서 32 MiB). 메모리 정책의 상한 안에 들어오지만 zero-copy는 아니다.
- 100%를 넘겨 확대하면 네이티브 픽셀을 보간해 키우는 것이므로 선이 부드러워질 뿐 해상도가 늘지는 않는다. 데스크톱과 같은 제약이며, 표시 배율로 재렌더하려면 엔진의 preview 정책부터 바꿔야 한다.
- undo 한도는 개수 기준이다. 엔진에 바이트 단위 히스토리 예산이 없어 정책 표의 MiB 값을 그대로 강제하지는 못한다.
