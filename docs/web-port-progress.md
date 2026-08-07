# Ugurugu 웹 이식 진행 현황

- 기준일: 2026-08-07
- 브랜치: `wasm-engine-spike`(main에 머지), 이후 `web-measure-recovery`
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

### 단계 1 잔여 측정 (2026-08-07, `web-measure-recovery` 브랜치, `bbe2d6c`/`2c96537`)

측정 도구는 `ugurugu_stress_document_generator`(1024/2048 스트레스 문서를 결정적으로 생성 — 4 레이어, 2,000 스트로크(Line/Airbrush/Spray, 일부 Erase), 200,000 포인트, 파일 약 11.3 MiB)와 `tools/wasm_engine_bench.mjs`(open·렌더·스트로크 지연과 wasm heap 측정)다. 측정 환경: macOS arm64, Node 24, 링크 최적화 적용 후 빌드.

| 항목 | Wave.ugu (640×400) | 스트레스 1024² | 스트레스 2048² |
|---|---:|---:|---:|
| 문서 열기 | 13 ms | 538 ms | 502 ms |
| 첫 전체 렌더 | 6 ms | 2.8 s | 5.6 s |
| 스트로크 시작 p95 | 4 ms | 5.7 s | 11.5 s |
| 스트로크 배치(append→렌더 반영) p50/p95 | 0.1/0.2 ms | 0.1/0.2 ms | 0.1/0.2 ms |
| 스트로크 커밋 p95 | 4 ms | 2.9 s | 5.7 s |
| serialize | 1 ms | 621 ms | 632 ms |
| wasm heap 피크 | 22 MiB | 103 MiB | 150 MiB |

핵심 해석:

- 진행 중 스트로크의 증분 경로(배치 append→dirty rect 반영)는 문서 밀도와 캔버스 크기에 거의 무관하게 0.2 ms 수준이다. 웹의 그리기 반응성 자체는 게이트를 통과한다.
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

### 검증 방법 (반복 실행 가능)

- Node 스모크: `node tools/wasm_engine_smoke.mjs [문서.ugu]` — load/render/round-trip과 해시 출력. 인자를 생략하면 `examples/Wave.ugu`.
- 네이티브 비교: `cmake --build --preset macos-debug --target ugurugu_engine_digest_probe && ./out/build/macos-debug/ugurugu_engine_digest_probe examples/Wave.ugu` — 스모크와 같은 형식의 해시.
- 측정: `cmake --build --preset macos-debug --target ugurugu_stress_document_generator`로 스트레스 문서를 만들고 `node tools/wasm_engine_bench.mjs <문서.ugu>…`로 지연·heap을 측정.
- 브라우저: `cd web && npm run build && npm run test:browser` — headless Chromium(`/Applications/Chromium.app`)으로 복구 루프(그리기→자동 저장→재접속→복구→픽셀 일치), PNG export, IndexedDB 실패 노출을 자동 검증. 이전 세션의 Worker 게이트·드로잉·레이어 시나리오도 같은 방식으로 검증했다.

## 2. 남은 작업

### 단계 1 잔여

- iOS Safari, Android Chrome 실제 장치 스모크
- Qt GPLv3 정적 배포 의무 검토 (출시 전)
- 웹 메모리 정책 수치의 확정(위 제안 표를 실기기 결과로 보정)과 셸에서의 실제 강제(import 검사, 새 문서 다이얼로그)

### 단계 2 잔여 (경계 강화)

- 현재 spike C ABI를 versioned command ABI로 정리하고 오류 모델 정의 (현 ABI는 안정 계약이 아님)
- native/Wasm 공통 fixture round-trip·픽셀 해시를 CI gate로 (현재는 수동 실행)

### 단계 3 잔여 (웹 UI)

- WebGL 2 presenter (현재 Canvas 2D `putImageData`)
- pan/zoom, 두 손가락 제스처, 모바일 반응형 레이아웃
- 채우기/선택 도구, 색 히스토리, 키보드 단축키
- 접근성: 키보드 전용 조작, 스크린 리더 레이블

### 단계 4 잔여 (배포)

- itch.io 패키징 검사(상대 경로·파일 수·크기)를 CI로, Butler push와 Restricted staging 프로젝트 스모크
- File System Access 지원 브라우저의 "같은 파일에 다시 저장" (선택 기능)

### 단계 5 (품질)

- 데스크톱 4종 브라우저·모바일 실기기 행렬, context loss/visibility 시험
- third-party notice와 대응 소스 제공 절차

## 3. 알려진 한계

- 진행 중 스트로크 프리뷰의 최초 프레임과 커밋 직후 프레임은 전체 렌더 1회씩을 쓴다(시작은 `renderLayerSplit`까지 포함해 사실상 2회). 스트레스 측정으로 정량화됨: 2,000 스트로크 밀도에서 시작 p95 5.7 s(1024²)/11.5 s(2048²), 커밋 p95 2.9/5.7 s. **웹 이식에서 가장 시급한 최적화 지점**이며, 후보는 커밋 시 split 재사용(전체 재렌더 대신 마지막 증분 상태 확정)과 wobble 프레임 캐시다. 배치 경로는 0.2 ms 수준이라 문제가 없다.
- 자동 복구 스냅샷은 스트로크 진행 중에는 건너뛰고 다음 주기에 저장한다. serialize가 수백 ms인 대형 문서에서는 스냅샷 주기 동안 Worker가 그 시간만큼 다른 명령을 받지 못한다.
- 웹 엔진 아티팩트(`ugurugu_engine_spike.{js,wasm}`)는 저장소에 커밋하지 않으며 `npm run sync-engine`이 `out/build/wasm-release`에서 복사한다.
- 지우개는 현재 기본 Line 설정 고정이다(`EraserPreset` 카탈로그 미연결).
