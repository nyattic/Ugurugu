# Ugurugu 웹 이식 진행 현황

- 기준일: 2026-08-07
- 브랜치: `wasm-engine-spike`
- 기준 계획: [web-itchio-feasibility.md](web-itchio-feasibility.md)의 권장 3번 방향과 단계 0~6

## 1. 완료된 작업

### 단계 1 수직 기술 spike — 핵심 게이트 통과

- 툴체인: Emscripten 4.0.7(`~/emsdk`), Qt 6.11.1 `wasm_singlethread`+호스트 `macos` 킷(`~/Qt/6.11.1`, aqtinstall, qtbase만). 빌드 방법은 BUILDING.md의 WebAssembly 절 참고.
- `c0941c9` — `UGURUGU_ENGINE_SOURCES`(Qt Core/Gui만으로 빌드)와 `UGURUGU_DESKTOP_SERVICE_SOURCES` 분리. 데스크톱 `ugurugu_core`는 두 목록의 합집합이라 기존 빌드 무변화(13개 테스트 스위트 전부 통과 확인).
- `c2cd8fd` — `wasm-release` 프리셋, `ugurugu_engine` 정적 라이브러리, `src/wasm/EngineBridge.cpp` C ABI, Node 스모크(`tools/wasm_engine_smoke.mjs`), 브라우저 Worker 하네스(`tools/wasm_worker_harness/`), 네이티브 비교 프로브(`ugurugu_engine_digest_probe`).

보고서가 "가장 중요한 미확인 가설"로 지정한 항목의 검증 결과:

| 가설 | 결과 |
|---|---|
| Qt Core/Gui Wasm을 Dedicated Worker에서 headless 구동 | 성립. QGuiApplication/QCoreApplication 인스턴스 없이 QImage/QPainter 렌더 동작, WebGL 요구 미발생 |
| 렌더 결정성 | `examples/Wave.ugu`(wobble 30프레임) 프레임 0/15/29 픽셀 sha256이 네이티브 macOS arm64와 **비트 동일**. wobble 픽스처조차 플랫폼별 baseline 불필요 |
| 직렬화 결정성 | 재직렬화 바이트가 네이티브와 **비트 동일**, 라운드트립 안정 |

- 릴리스 wasm 크기 9.4 MB(비압축, LTO/Brotli 미적용).
- 발견한 함정: Qt wasm 정적 라이브러리는 내부적으로 emscripten::val을 쓰므로 plain `add_executable` 링크에 `-lembind`가 필요하다. Qt 6.11.1의 wasm 온라인 패키지는 Windows에서 빌드된 단일 아카이브 하나뿐이며(파일명의 `Windows-…-WebAssembly`), 호스트 도구는 `QT_HOST_PATH`가 공급하므로 macOS에서 그대로 쓴다.

### 웹 셸 (단계 3 골격)

- `d759710` — UI 프레임워크를 React에서 **Svelte 5**로 확정, 보고서 7.1 갱신.
- `8c35d8e` — `web/`에 Vite+Svelte 5+TypeScript strict 셸: `EngineClient`(id 기반 Worker RPC) ↔ `public/engine/engine-worker.js`(Wasm 엔진 소유, premultiplied BGRA→straight RGBA 변환) ↔ 캔버스 표시. 데모 문서 자동 로드, 프레임 슬라이더, 문서 fps 재생, `.ugu` 열기/Blob 다운로드.
- `0a66d66` — 드로잉. Pointer Events(`setPointerCapture`, `getCoalescedEvents`, 펜 압력) → Worker 배치 전송 → 진행 중 스트로크는 문서 COW 복사본 위 프리뷰, 떼면 `DocumentController::addStroke`로 히스토리 커밋. 실행 취소/다시 실행은 데스크톱과 같은 `DocumentUndoStack`.
- `ade8ccd` — 브러시 프리셋(`BrushPresetCatalog` 그대로), 손떨림 보정(`StrokeStabilizer`), 레이어 패널(추가/삭제/이동/이름변경/표시/불투명도, 전부 controller 경유라 undo 대상), 증분 프리뷰(`IncrementalStrokeRenderer` 타일 + `renderLayerSplit`/`composeLayerSplitRegion`, dirty rect만 변환·전송, split 불가 문서는 전체 렌더 폴백).

셸 번들 48 KB(gzip 18.6 KB). 실행: `cmake --build --preset wasm-release && cd web && npm install && npm run dev`.

### 검증 방법 (반복 실행 가능)

- Node 스모크: `node tools/wasm_engine_smoke.mjs` — load/render/round-trip과 해시 출력.
- 네이티브 비교: `cmake --build --preset macos-debug --target ugurugu_engine_digest_probe && ./out/build/macos-debug/ugurugu_engine_digest_probe examples/Wave.ugu` — 스모크와 같은 형식의 해시.
- 브라우저: headless Chromium(Playwright-core)으로 Worker 게이트, 합성 포인터 드로잉(그리기→undo 0픽셀→redo 동일 픽셀 수), 레이어 추가·표시 토글 시나리오를 자동 검증했다.

## 2. 남은 작업

### 단계 1 잔여 (측정)

- 1024/2048 스트레스 문서의 peak memory, 첫 입력 지연, stroke latency p95 측정 후 웹 메모리 정책 수치 확정
- 릴리스 빌드 LTO/size 최적화와 gzip/Brotli 전송 크기 측정 (itch.io 단일 파일 200 MB 한도 대비)
- iOS Safari, Android Chrome 실제 장치 스모크
- Qt GPLv3 정적 배포 의무 검토 (출시 전)

### 단계 2 잔여 (경계 강화)

- 현재 spike C ABI를 versioned command ABI로 정리하고 오류 모델 정의 (현 ABI는 안정 계약이 아님)
- native/Wasm 공통 fixture round-trip·픽셀 해시를 CI gate로 (현재는 수동 실행)

### 단계 3 잔여 (웹 UI)

- WebGL 2 presenter (현재 Canvas 2D `putImageData`)
- pan/zoom, 두 손가락 제스처, 모바일 반응형 레이아웃
- 채우기/선택 도구, 색 히스토리, 키보드 단축키
- 접근성: 키보드 전용 조작, 스크린 리더 레이블

### 단계 4 (파일·복구·배포)

- IndexedDB 자동 복구 슬롯과 quota 오류 UX
- itch.io 패키징 검사(상대 경로·파일 수·크기)를 CI로, Butler push와 Restricted staging 프로젝트 스모크
- 현재 프레임 PNG export

### 단계 5 (품질)

- 데스크톱 4종 브라우저·모바일 실기기 행렬, context loss/visibility 시험
- third-party notice와 대응 소스 제공 절차

## 3. 알려진 한계

- 진행 중 스트로크 프리뷰의 최초 프레임과 커밋 직후 프레임은 전체 렌더 1회씩을 쓴다. 큰 캔버스에서 스트로크 시작/종료 지연이 문제가 되면 여기가 다음 최적화 지점이다.
- 웹 엔진 아티팩트(`ugurugu_engine_spike.{js,wasm}`)는 저장소에 커밋하지 않으며 `npm run sync-engine`이 `out/build/wasm-release`에서 복사한다.
- 지우개는 현재 기본 Line 설정 고정이다(`EraserPreset` 카탈로그 미연결).
