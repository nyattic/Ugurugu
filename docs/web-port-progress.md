# Ugurugu 웹 이식 진행 현황

- 기준일: 2026-08-08
- 브랜치: `wasm-engine-spike`·`web-measure-recovery`(main에 머지), 이후 `web-port-next`
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
- `web/src/lib/ViewTransform.ts` — 커서 고정 확대, 이동, 화면 맞춤. 휠 이동, Ctrl/Cmd+휠 확대, 스페이스·가운데 버튼 드래그 이동, 두 손가락 핀치 확대·이동을 지원한다. 두 번째 손가락이 닿으면 진행 중 스트로크는 커밋된다.
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
- ABI를 3으로 올리고 다음을 추가했다. 페인트통 `ugu_bucket_fill`, 올가미/사각/타원 `ugu_selection_shape`(데스크톱 LassoMode의 Paint 모드 포함), 마술봉 `ugu_selection_flood`, `ugu_selection_all`/`_invert`/`_clear`/`_fill`/`_delete`, 윤곽 읽기 `ugu_selection_outline{,_size}`, 옵션 `ugu_set_fill_options`. 상태 코드에 9(선택 없음), 10(빈 영역), 11(레이어에 그릴 수 없음)이 늘었다.
- 선택은 브리지가 캔버스 크기 Grayscale8 마스크와 소속 레이어로 들고 있으며, `ugu_stroke_begin`이 그 마스크를 스트로크의 `clipMask`로 실어 보낸다. `CanvasWidget::beginStroke`와 같은 계약이라 증분 프리뷰와 커밋 렌더가 모두 선택 경계에서 잘린다. 페인트통과 선택 영역 채우기도 같은 마스크로 클립한다.
- 윤곽은 닫힌 컨투어를 `[정점 수, x, y, …]`로 이어 붙인 float 버퍼로 넘긴다. 워커가 `selectionRevision`을 비교해 **바뀐 경우에만** 읽어 보내므로 스트로크 중 매 응답마다 마스크를 훑지 않는다. 순수 선택 변경은 픽셀을 옮기지 않으므로 이미지 데이터 없는 응답으로 답한다.
- UI를 다시 배치했다. 도구가 여섯 개로 늘어 상단 바가 감당하지 못하므로 **좌측 세로 레일**(아이콘은 데스크톱 `src/ui/Icons.cpp` 글리프를 SVG로 옮긴 것)과 그 옆의 도구별 옵션 열로 나눴다. 팔레트는 데스크톱 `Theme.cpp` 값(그래파이트 + 앰버 `#FFC94A`)을 그대로 쓴다. 활성 도구 테두리는 프레임마다 다시 그려지는 손그림 윤곽이며 `prefers-reduced-motion`에서는 첫 프레임으로 고정된다.
- 마칭 앤츠는 표시 캔버스 위 오버레이 캔버스가 그리고, 선택이 없으면 애니메이션 루프가 돌지 않는다. 드래그 중인 올가미 경로도 같은 오버레이에 그린다.
- 단축키는 데스크톱 레일과 같다. B/E/L/W/G/I, Ctrl+A 전체 선택, Ctrl+Shift+I 반전, Ctrl+D·Esc 해제, Delete 삭제, Alt+Delete 채우기.

알려진 차이: 웹의 선택 상태는 셸이 들고 있어 **실행 취소 대상이 아니다**. 데스크톱은 `pushSelectionStateCommand`로 선택 전환까지 히스토리에 넣는다. 선택 영역 이동·변형(`transformSelection`)도 아직 웹에 없다.

### 검증 방법 (반복 실행 가능)

- Node 스모크: `node tools/wasm_engine_smoke.mjs [문서.ugu]` — load/render/round-trip과 해시 출력. 인자를 생략하면 `examples/Wave.ugu`.
- 네이티브 비교: `cmake --build --preset macos-debug --target ugurugu_engine_digest_probe && ./out/build/macos-debug/ugurugu_engine_digest_probe examples/Wave.ugu` — 스모크와 같은 형식의 해시.
- 측정: `cmake --build --preset macos-debug --target ugurugu_stress_document_generator`로 스트레스 문서를 만들고 `node tools/wasm_engine_bench.mjs <문서.ugu>…`로 지연·heap을 측정.
- 브라우저: `cd web && npm run build && npm run test:browser` — headless Chromium(`/Applications/Chromium.app`)으로 복구 루프(그리기→자동 저장→재접속→복구→픽셀 일치), 드래그 중 라이브 프리뷰, 레이어 썸네일 표시·갱신, 컬러 서클/최근 색/스포이드, PNG·GIF 다운로드 서명, IndexedDB 실패 노출, 확대·축소와 확대 시 문서 픽셀 불변, B/E/I·Ctrl+Z 단축키, 지우개 프리셋, 새 문서 생성과 상한 클램프, 스트로크 후 재생 유지, 안티앨리어싱 토글, 캔버스 밖으로 나간 스트로크 커밋, 그리고 올가미 선택→마칭 앤츠 표시→선택 영역 채우기(140×100 = 14,000 px 정확히), 선택 경계에서 멈추는 스트로크(오른쪽 끝 x=439), 선택 영역 삭제, 빈 레이어 전체를 채우는 페인트통(307,200 px)과 그 실행 취소, 올가미 Paint 모드, 마술봉, L/W/G/B 단축키를 자동 검증(48개 체크).
- itch.io 패키징: `cd web && npm run build && node ../tools/check_itchio_package.mjs dist`.

## 2. 남은 작업

계정·실기기·법률 검토가 필요한 항목이 남은 작업의 대부분이다. 코드로 닫을 수 있는 항목은 도구·기능 추가 쪽에 몰려 있다.

### 실기기·계정·법률 (코드로 닫을 수 없음)

- iOS Safari, Android Chrome 실제 장치 스모크. 단계 1의 중단 기준 판단 재료이자 Mobile Friendly 표시의 전제다.
- itch.io Restricted staging 프로젝트에 Butler push, 페이지 내/전체 viewport 실행, iframe origin·`crossOriginIsolated`·clipboard·fullscreen 실측. 패키징 검사는 CI에 있지만 **아직 한 번도 업로드하지 않았다**.
- 데스크톱 4종 브라우저 행렬, context loss/visibility 시험. WebGL 컨텍스트 손실 시 Canvas 2D 폴백 경로는 구현했으나 실제 손실 상황에서는 검증하지 못했다.
- Qt GPLv3 정적 배포 의무 검토, third-party notice와 대응 소스 제공 절차 (출시 전 필수).
- 메모리 정책 수치를 실기기 결과로 보정. 현재 값은 측정 기반 제안치를 그대로 코드에 옮긴 것이다.

### 단계 3 잔여 (웹 UI)

- 웹 UI 번역 계층 (현재 영어 고정, 데스크톱은 ko/en/ja)
- 선택 영역 이동·확대·회전 (`transformSelection`)과 선택 전환의 실행 취소
- 모바일 반응형 레이아웃. 핀치·이동 제스처는 있으나 패널 배치는 데스크톱 고정이다.
- 접근성 마무리. 캔버스·슬라이더 레이블과 전 기능 단축키는 넣었지만, 스크린 리더 낭독 순서와 레이어 트리의 키보드 전용 조작은 남아 있다.

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
