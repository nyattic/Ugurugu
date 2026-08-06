# Ugurugu 코드 감사 보고서 — 2026-08-06

## 요약

- 감사 기준은 `main`과 `origin/main`이 함께 가리키던 커밋 `6d0ba0d8a5ae91dad76f22aaf4351642987aafa8`이다.
- 이전 감사 문서나 결론을 근거로 사용하지 않고, `src`, `tests`, 빌드 설정과 주요 호출 경로를 기준 커밋부터 다시 추적했다.
- 확정 결함은 12개다: 높음 5개, 중간 6개, 낮음 1개다. 추측만 가능한 항목은 finding에서 제외했다.
- 12개 결함을 모두 수정했고 각 결함에 회귀 테스트를 추가했다. 감사 종료 시점에 알려진 미수정 확정 결함은 없다.
- 최종 작업트리는 Debug, Release, ASan+UBSan 전체 13개 CTest suite, clang-format, clang-tidy 165개 번역 단위, macOS 패키지 smoke test를 모두 통과했다.

| ID | 심각도 | 신뢰도 | 상태 | 요약 |
|---|---|---|---|---|
| UGU-2026-001 | 높음 | 높음 | 수정 및 회귀 테스트 완료 | 이미지 전용 레이어가 이미지/캔버스 크기 변경 때 삭제됨 |
| UGU-2026-002 | 높음 | 높음 | 수정 및 회귀 테스트 완료 | Wawa 레이어 PNG를 누적 보존해 import 메모리 예산을 우회함 |
| UGU-2026-003 | 높음 | 높음 | 수정 및 회귀 테스트 완료 | 내보내기 메모리 정책이 composite section과 큰 reframe epoch를 누락함 |
| UGU-2026-004 | 높음 | 높음 | 수정 및 회귀 테스트 완료 | preview 메모리 정책이 display-scale reframe epoch를 누락함 |
| UGU-2026-005 | 높음 | 높음 | 수정 및 회귀 테스트 완료 | packed fill 해제 마스크를 레이어 끝까지 캐시해 약 2 GiB까지 누적함 |
| UGU-2026-006 | 중간 | 높음 | 수정 및 회귀 테스트 완료 | 선택 클립보드가 이미지 asset과 source motion을 직렬화하지 않음 |
| UGU-2026-007 | 중간 | 높음 | 수정 및 회귀 테스트 완료 | 크기 검사 뒤 `readAll()`하여 입력 상한을 TOCTOU로 우회할 수 있음 |
| UGU-2026-008 | 중간 | 높음 | 수정 및 회귀 테스트 완료 | 유효한 장선에서 실수→`int` 변환과 signed 덧셈이 UB를 일으킴 |
| UGU-2026-009 | 중간 | 높음 | 수정 및 회귀 테스트 완료 | 선택 변형 직후 또는 비동기 평가 뒤 undo가 selection mask 전환을 놓침 |
| UGU-2026-010 | 중간 | 높음 | 수정 및 회귀 테스트 완료 | Image/CompositeBoundary 레이어의 선택 가시성 평가가 실패함 |
| UGU-2026-011 | 중간 | 높음 | 수정 및 회귀 테스트 완료 | square cap/miter가 증분 렌더 타일 경계를 벗어남 |
| UGU-2026-012 | 낮음 | 높음 | 수정 및 회귀 테스트 완료 | non-AA packed fill의 sparse coverage가 1px halo를 포함함 |

## 감사 범위와 방법

다음 경로를 중심으로 전체 호출 관계와 데이터 불변식을 읽었다.

- 저장/불러오기: document JSON 준비·검증·쓰기, Wawa reader/importer, raster/mask asset table, 프리셋 입출력
- autosave/복구: `RecoveryStore`, `RecoveryWriter`, MainWindow lifecycle, pending selection snapshot, `QSaveFile` commit/error 경로
- undo/redo: document delta 생성·적용, history memory, selection state transition, layer/stroke command
- 선택 영역 변형: mask packing, visibility worker 취소와 완료 순서, PixelSelection replay, copy/cut/paste
- 레이어: 계층 검증, clip/group composition, merge boundary, resize/reframe, raster asset 수명
- 렌더링/내보내기: native/display-scale replay, incremental bounds, stroke coverage, GIF/WebP worker와 메모리 정책
- 메모리 수명·경계값·오류 처리: worker cancellation/receiver lifetime, 이미지·마스크 예산, 좌표·개수 상한, 파일 오류 전파

기준 커밋의 기존 테스트가 모두 통과한 뒤에도, 각 후보를 최소 문서나 독립 sanitizer 재현으로 확인했다. 실제 픽셀 차이, sanitizer 진단, 유효 문서의 상태 불일치 또는 산술적으로 증명되는 예산 초과가 없는 후보는 finding으로 채택하지 않았다.

## 확정 결함과 수정

### UGU-2026-001 — 이미지 전용 레이어가 크기 변경 때 삭제됨

- 심각도: **높음**
- 신뢰도: **높음**
- 위치: `src/document/DocumentController.cpp:88-97`, `src/document/DocumentController.cpp:708-759`, `src/document/DocumentController.cpp:776-841`
- 회귀 테스트: `tests/DocumentResizeTests.cpp:234`, `tests/DocumentResizeTests.cpp:271`
- 발생 조건/재현:
  1. `insertImage()`로 Image operation 하나만 가진 레이어를 만든다.
  2. 이미지 크기 변경 또는 캔버스 크기 변경을 실행한다.
  3. 수정 전 `layerCanProducePixels()`는 Paint와 Fill만 인정했다. 따라서 해당 레이어의 stroke를 모두 지우고 이후 미참조 raster asset도 제거했다.
- 실제 영향: 사용자가 삽입한 이미지 레이어가 정상적인 resize 명령 하나로 조용히 사라진다. 그 상태로 저장하면 데이터 손실이 영구화된다.
- 권장 수정 방법: 픽셀을 생성하는 operation에 Image를 포함하고, 다른 픽셀 레이어와 마찬가지로 trailing Reframe operation을 기록해야 한다.
- 적용한 수정: Image를 생산 가능한 operation으로 분류했다. 이미지/캔버스 resize 뒤 Image+Reframe 순서, asset 보존, 렌더 결과, undo/redo 왕복을 두 회귀 테스트로 검증했다.

### UGU-2026-002 — Wawa import가 decoded layer image를 무제한 누적함

- 심각도: **높음**
- 신뢰도: **높음**
- 위치: `src/io/WawaV10Reader.cpp:164-249`, `src/io/WawaV10Reader.cpp:364-483`
- 회귀 테스트: `tests/WawaV10ReaderTests.cpp:256`
- 발생 조건/재현: Wawa v10이 허용하는 최대 4096×4096 PNG 레이어를 서로 다른 내용으로 여러 개 넣는다. reader는 최대 10개 레이어의 decoded/canonical 이미지를 importer가 asset budget을 적용하기 전까지 모두 보존할 수 있었다. 한 장은 약 64 MiB이므로 보존 이미지만 약 640 MiB이고 decode/canonical 임시 backing이 추가된다.
- 실제 영향: 파일 자체는 128 MiB 입력 상한 안에 있어도 import 중 과도한 메모리 사용, UI 정지 또는 OOM이 가능하다.
- 권장 수정 방법: reader 단계에서 distinct decoded byte budget을 적용하되 동일 내용과 완전 투명 이미지는 중복 과금하지 않아야 한다. 중복 여부를 확인하는 현재 한 장만 budget 외 transient로 허용해야 한다.
- 적용한 수정: canonical content ID로 이미지를 deduplicate하고 implicit sharing을 사용했다. 완전 투명 이미지는 한 장의 공유 blank로 정규화하며 distinct budget을 소비하지 않는다. 새 visible content만 `maximumDistinctRasterDecodedBytes`에서 차감한다. 동일 이미지 2개, 서로 다른 이미지 2개, RGB가 다른 alpha-zero 이미지 2개를 모두 회귀 테스트했다.

### UGU-2026-003 — export 정책이 composite section과 큰 reframe epoch를 누락함

- 심각도: **높음**
- 신뢰도: **높음**
- 위치: `src/render/LayerCompositionPlan.cpp:151-222`, `src/render/LayerCompositionPlan.cpp:339-375`, `src/io/RenderExportPolicy.cpp:23-111`
- 회귀 테스트: `tests/GifWriterTests.cpp:144`, `tests/GifWriterTests.cpp:168`
- 발생 조건/재현:
  - 4096×4096 문서, 8 frame, 한 paint layer에 CompositeBoundary 9개, 1024×1024 GIF 출력: 수정 전 정책은 약 140 MiB로 계산했지만 native section backing만 9×64 MiB다.
  - 최종 크기는 1024×1024지만 initial canvas가 4096×4096이고 4096→2048→4096→1024 reframe을 거치는 merged layer: 수정 전에는 최종 surface 크기를 중심으로 계산해 큰 epoch와 old/new surface 공존을 놓쳤다.
- 실제 영향: 512 MiB export guard가 통과한 뒤 실제 렌더가 guard보다 많은 메모리를 사용하여 할당 실패나 OOM을 낼 수 있다.
- 권장 수정 방법: layer composition plan이 최대 native/display epoch 크기, composite section peak, paint scratch surface 공존을 추적하고 static/native/scaled animation 정책이 그 값을 사용해야 한다.
- 적용한 수정: plan에 paint surface 크기와 section peak를 추가하고 export 계산을 교체했다. 위 재현의 최종 추정치는 각각 772 MiB, static 712 MiB, native GIF 736 MiB, 256×256 GIF 708.25 MiB이며 모두 512 MiB budget에서 거부된다.

### UGU-2026-004 — preview 정책이 큰 display-scale reframe epoch를 누락함

- 심각도: **높음**
- 신뢰도: **높음**
- 위치: `src/render/PreviewRenderPolicy.cpp:54-127`, `src/render/LayerCompositionPlan.cpp:349-375`, `src/ui/CanvasWidgetPreview.cpp:531-582`
- 회귀 테스트: `tests/RenderPreviewTests.cpp:161`
- 발생 조건/재현: 최종 1024×1024, initial 4096×4096 문서를 128×128 preview로 렌더한다. DisplayScaleReplay에서 initial epoch는 512×512이고, composite boundary가 많을수록 그 surface가 동시에 여러 장 남는다. 수정 전 정책은 128×128 최종 surface만 기준으로 삼았다.
- 실제 영향: preview cache의 90% working budget을 넘겨 UI 정지 또는 OOM을 일으킬 수 있다.
- 권장 수정 방법: 단순 문서 크기가 아니라 전체 Document composition plan을 입력으로 받고, 모든 display epoch와 retained/section/scratch surface를 계산해 preview를 축소해야 한다. 1px 최소 크기에서도 budget을 넘으면 안전하게 실패해야 한다.
- 적용한 수정: document-aware `renderSize()`를 추가했다. 실제 renderer stats로 512×512 intermediate를 확인했고, 40-boundary 문서는 budget 안 크기로 축소되며 9000-boundary 극단 문서는 빈 QSize를 반환한다.

### UGU-2026-005 — packed fill unpack 결과가 레이어 끝까지 누적됨

- 심각도: **높음**
- 신뢰도: **높음**
- 위치: `src/render/engine/LayerOperationReplay.cpp:57-121`, `src/render/engine/LayerOperationReplay.cpp:245-268`
- 회귀 테스트: `tests/StrokeRenderingTests.cpp:249`
- 발생 조건/재현: 4096×4096 full-canvas packed fill mask는 약 2 MiB라 256 MiB packed-mask 예산 안에 128개를 넣을 수 있다. 수정 전 각 mask를 16 MiB Grayscale8로 unpack한 뒤 서로 다른 cache key로 `scaledClipMasks`에 보존하여 약 2 GiB까지 누적했다.
- 실제 영향: preview와 export가 공유하는 replay 경로에서 정책에 잡히지 않는 대규모 retained memory와 OOM이 발생할 수 있다.
- 권장 수정 방법: document-owned reusable mask만 캐시하고, packed fill에서 현재 stroke를 위해 decode한 coverage는 stroke 종료 때 해제해야 한다.
- 적용한 수정: packed coverage를 transient scaling 경로로 분리했다. 서로 다른 packed fill 2개에서 peak backing 1개, persistent cache 0개, 두 픽셀의 정상 렌더를 회귀 테스트로 확인했다.

### UGU-2026-006 — 선택 클립보드가 raster asset과 motion을 누락함

- 심각도: **중간**
- 신뢰도: **높음**
- 위치: `src/io/SelectionClipboardCodec.cpp:84-161`, `src/io/SelectionClipboardCodec.cpp:165-198`, `src/document/DocumentControllerLayers.cpp:543-710`, `src/ui/CanvasWidget.cpp:609-619`, `src/ui/MainWindow.cpp:1145-1153`
- 회귀 테스트: `tests/SelectionClipboardTests.cpp:106`, `tests/SelectionClipboardTests.cpp:143`
- 발생 조건/재현:
  - Image operation이 포함된 레이어 선택을 복사하면 payload document에 참조 raster asset이 없어 직렬화/붙여넣기가 실패하거나 유효하지 않은 레이어가 된다.
  - 비기본 smooth/linked/random motion을 쓰는 animated stroke를 복사하면 PNG clipboard representation과 payload 재렌더가 기본 motion으로 계산된다.
- 실제 영향: 삽입 이미지가 있는 선택 영역을 정상적으로 복사·붙여넣을 수 없고, 외부 앱이 받는 raster나 직렬화 payload의 frame 모양이 원본과 달라질 수 있다.
- 권장 수정 방법: 선택 레이어가 실제 참조하는 asset만 payload에 포함하고, 붙여넣기 전에 ID/payload 일치와 전체 document budget을 검증해야 한다. raster를 생성하는 payload에는 source motion도 복사해야 한다.
- 적용한 수정: 참조 asset subset과 motion을 payload에 포함했다. paste는 기존 동일 asset을 deduplicate하고 ID 충돌이나 잘못된 payload를 원자적으로 거부하며 history delta가 asset 추가/제거를 함께 undo/redo한다.

### UGU-2026-007 — 파일 크기 선검사 뒤 unbounded read를 수행함

- 심각도: **중간**
- 신뢰도: **높음**
- 위치: `src/io/DocumentSerializer.cpp:619-648`, `src/ui/MainWindow.cpp:289-330`, `src/ui/MainWindowSettings.cpp:398-417`
- 회귀 테스트: `tests/SerializationBudgetTests.cpp:1087`
- 발생 조건/재현: `.ugu`/`.wawa`의 128 MiB 또는 `.wwpreset`의 1 MiB `size()` 검사가 끝난 뒤 파일을 늘리거나 교체한다. 수정 전 `readAll()`은 검사했던 크기와 무관하게 EOF까지 읽었다. stale size를 반환하는 QIODevice로 동일 조건을 결정적으로 재현했다.
- 실제 영향: 후속 parser가 초과 payload를 거부하더라도 그 전에 UI thread의 과도한 할당, 정지 또는 OOM이 발생할 수 있다. 문서 형식 검증 우회가 아니라 입력 읽기 예산 우회다.
- 권장 수정 방법: 최대값+1 바이트만 읽고 실제 길이와 EOF를 함께 검사해야 한다. 음수/불명 크기도 거부해야 한다.
- 적용한 수정: 세 입력 경로를 bounded read로 바꾸고 serializer에 작은 test seam을 추가했다.

### UGU-2026-008 — 장선의 noise cell 계산이 C++ UB를 일으킴

- 심각도: **중간**
- 신뢰도: **높음**
- 위치: `src/render/BrokenLineModel.cpp:43-72`, `src/render/DeterministicNoise.cpp:22-61`, `src/render/DeterministicNoise.hpp:7-13`
- 회귀 테스트: `tests/BrokenLineModelTests.cpp:79`
- 발생 조건/재현: 허용 범위인 ±`maximumStoredCoordinateMagnitude`를 번갈아 가는 50,000개 point와 작은 break range를 사용하면 누적 arc cell이 `INT_MAX`를 넘는다. 수정 전 UBSan은 `BrokenLineModel.cpp`의 실수→`int` 변환에서 중단했다. `smoothValue(INT_MAX + 1.5)`도 같은 계열을 재현했다.
- 실제 영향: 유효한 문서가 sanitizer에서 중단되고, 최적화 설정에 따라 broken-line 가시 구간이 잘못되거나 비결정적 결과가 날 수 있다. `frame + 1`, `index + 4099`, `left + 1`에는 signed overflow 가능성도 있었다.
- 권장 수정 방법: cell/index를 `qint64`로 전파하고 finite/qint64 범위를 검사하며 hash 산술은 정의된 unsigned wrap으로 수행해야 한다.
- 적용한 수정: 위 방식으로 변경했고 50,000 point 결과의 크기·결정성·tail 분포와 경계 `smoothValue`를 검증했다. 수정 후 UBSan 재현은 진단 없이 종료했다.

### UGU-2026-009 — undo/redo가 selection overlay 전환을 놓침

- 심각도: **중간**
- 신뢰도: **높음**
- 위치: `src/ui/CanvasWidgetSelection.cpp:1018-1055`
- 회귀 테스트: `tests/UiSelectionTests.cpp:2038`, `tests/UiSelectionTests.cpp:2090`
- 발생 조건/재현:
  - 선택 변형을 적용한 직후 visibility worker가 완료되기 전에 Undo한다.
  - 또는 선택한 실제 픽셀은 전부 캔버스 밖으로 이동시키되 selection mask 일부는 남긴다. 비동기 평가가 완료되어 selected stroke ID를 비운 뒤 Undo한다.
  - 수정 전 handler는 현재 selected ID가 `fromStrokeIds`와 겹치는 경우에만 mask를 바꿨다. 첫 조건에서는 prune/cancellation 순서에, 둘째 조건에서는 정상적인 빈 가시성 결과에 의해 gate가 false가 됐다.
- 실제 영향: document pixels와 history index는 undo/redo되지만 selection mask는 반대 시점 위치에 남아 다음 변형·삭제 대상과 화면 overlay가 실제 문서 상태와 어긋난다.
- 권장 수정 방법: 동일 layer이고 현재 mask가 history transition의 정확한 `fromMask`임을 확인했다면 selected-ID cache 상태와 무관하게 `from→to` overlay를 적용해야 한다.
- 적용한 수정: 이미 존재하는 layer/mask/format 검증 뒤의 ID gate를 제거했다. 즉시 Undo/Redo와 비동기 빈 결과 완료 후 Undo/Redo를 각각 고정했다.

### UGU-2026-010 — Image/CompositeBoundary 레이어의 선택 감지가 실패함

- 심각도: **중간**
- 신뢰도: **높음**
- 위치: `src/document/SelectionVisibility.cpp:40-63`, `src/document/SelectionVisibility.cpp:197-238`, `src/render/RenderEngineStrokes.cpp:21-65`, `src/render/RenderEngineStrokes.cpp:109-134`
- 회귀 테스트: `tests/StrokeCoverageTests.cpp:13`, `tests/StrokeCoverageTests.cpp:43`
- 발생 조건/재현:
  - 삽입 이미지 레이어에서 이미지가 있는 영역을 선택한다.
  - 또는 Paint→CompositeBoundary→Paint 순서인 merge 결과 레이어를 선택한다.
  - 수정 전 visibility evaluator는 이 operation들을 regional-safe로 분류했지만 regional renderer는 거부했다. full stroke renderer도 Image를 framebuffer operation 목록에서 빠뜨려 empty points인 Image를 건너뛰었다.
- 실제 영향: 실제 픽셀이 있는데도 UI가 “선택 영역에 내용 없음”으로 판단하여 변형·삭제를 비활성화한다. Image는 기본 brush wobble 때문에 불필요하게 전체 animation frame을 검사할 수도 있었다.
- 권장 수정 방법: Image와 CompositeBoundary는 full framebuffer replay로 보내고 정적 operation으로 분류해야 한다. regional renderer는 명시적으로 거부해야 한다.
- 적용한 수정: 세 분류를 일치시켰고 30-frame/wobble 문서에서도 Image와 merged layer가 한 frame만 렌더해 visible/succeeded가 되는지 확인했다.

### UGU-2026-011 — square cap/miter가 증분 렌더 경계를 벗어남

- 심각도: **중간**
- 신뢰도: **높음**
- 위치: `src/render/StrokeRenderer.cpp:997-1020`
- 회귀 테스트: `tests/LayerSplitPreviewTests.cpp:351`
- 발생 조건/재현: 512×512 투명 캔버스에 width 200, square/non-AA, `(100,100)→(140,140)` 선을 증분 렌더한다. 수정 전 full render와 1,300 pixel이 달랐고 첫 차이는 `(256,115)`에서 expected alpha 255, actual alpha 0이었다. acute multi-segment MiterJoin도 동일 계열이다.
- 실제 영향: 그리는 동안 square cap/join 일부가 잘리거나 깜빡이고 전체 rerender 후에만 복구된다.
- 권장 수정 방법: primitive dirty bounds가 Qt `QPen`의 square cap과 기본 miter limit의 최대 reach를 보수적으로 포함해야 한다.
- 적용한 수정: square tip reach를 `2.1 * width + 3`으로 확장하고 2-point cap 및 acute join의 incremental 결과를 full render와 pixel 단위로 비교했다.

### UGU-2026-012 — non-AA packed fill sparse coverage에 1px halo가 생김

- 심각도: **낮음**
- 신뢰도: **높음**
- 위치: `src/render/StrokeCoverageRenderer.cpp:638-704`
- 회귀 테스트: `tests/StrokeCoverageTests.cpp:328`
- 발생 조건/재현: 32×32 캔버스에서 packed coverage의 `(10,10)` 한 픽셀만 켜고 antialiasing을 끈다. 수정 전 sparse coverage는 실제 exact 렌더에 없는 상하좌우 4픽셀을 포함했다. 예를 들어 `(9,10)`은 exact alpha 0, sparse alpha 255였다.
- 실제 영향: 실제 렌더 밖 1px가 선택 가시성·편집 가능 영역 또는 부분 갱신 대상으로 잘못 포함된다.
- 권장 수정 방법: legacy/procedural fill 또는 AA fill에서만 이웃 edge coverage를 추가하고, non-AA packed coverage는 mask와 정확히 일치시켜야 한다.
- 적용한 수정: edge 조건을 분리하고 sparse 결과를 exact coverage와 비교하는 회귀 테스트를 추가했다.

## 수정 후 검증 결과

### 기준선

수정 전에도 기존 suite는 모두 통과했다. 따라서 위 결함은 기존 회귀 범위의 공백이었다.

- Debug configure/build 성공
- Debug CTest 13/13 통과
- ASan+UBSan CTest 13/13 통과
- clang-format과 clang-tidy 기준선 통과

### 최종 작업트리

| 검증 | 명령 | 결과 |
|---|---|---|
| HEAD 일치 | `git rev-parse HEAD`, `git rev-parse origin/main` | 둘 다 `6d0ba0d8a5ae91dad76f22aaf4351642987aafa8` |
| Debug configure | `cmake --preset macos-debug` | 성공 |
| Debug build | `cmake --build --preset macos-debug` | 성공 |
| Debug tests | `ctest --preset macos-debug --output-on-failure -j 4` | 13/13 통과, 14.69초 |
| Format | `cmake --build --preset macos-debug --target ugurugu_format_check` | 통과 |
| Clang-Tidy | `cmake --build --preset macos-debug --target ugurugu_tidy` | 165/165 번역 단위 통과, 진단 없음 |
| ASan+UBSan configure/build | `cmake --preset macos-sanitized`, `cmake --build --preset macos-sanitized` | 성공 |
| ASan+UBSan tests | `ctest --preset macos-sanitized` | 13/13 통과, 102.48초; `halt_on_error=1` |
| Release configure/build | `cmake --preset macos-release`, `cmake --build --preset macos-release` | 성공 |
| Release tests | `ctest --preset macos-release --output-on-failure -j 4` | 13/13 통과, 37.96초 |
| Package smoke | `cmake --build --preset macos-release --target ugurugu_package_smoke_test` | 설치, runtime probe, deep strict codesign 검증 통과 |
| Patch hygiene | `git diff --check` | 통과 |

Configure 때 Vulkan headers 미탐지와 Qt GuiPrivate 버전 결합 경고가 출력됐지만 둘 다 이 저장소의 현재 macOS 구성에서 비치명적이며 빌드·테스트 실패는 아니었다.

### 호스팅 macOS sanitizer 후속 검증

감사 커밋 `3a100f02fc43656de9b44c19e4aaf6722c89a6b1`의 첫 GitHub Actions 실행에서 macOS 15.7.7/AppleClang 17의 `ugurugu_ui_viewport_tests`가 4K frame-cache warmup 테스트를 마친 직후 SIGTRAP으로 종료됐다. 출력은 제품 코드의 ASan/UBSan 진단이 아니라 compiler-rt의 [`UnsetAlternateSignalStack()`](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/sanitizer_common/sanitizer_posix_libcdep.cpp)이 worker pthread 종료 중 `sigaltstack(SS_DISABLE, ...)` 실패를 내부 `CHECK`로 처리한 것이었다. 테스트 본문의 마지막 진단 로그까지 도달했고, 이전 커밋의 같은 경로는 동일 runner 계열에서 통과했으며, macOS 26.6/AppleClang 21에서는 해당 suite를 20회, 문제 테스트를 30회 반복해 모두 통과했다.

[`QThreadPool` 소멸자](https://doc.qt.io/qt-6/qthreadpool.html#dtor.QThreadPool)는 이미 모든 runnable 완료와 worker 종료를 기다리므로 제품 코드에 중복 `waitForDone()`을 추가하지 않았다. 대신 macOS ASan+UBSan test preset에 compiler-rt의 공식 [`use_sigaltstack=0`](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/sanitizer_common/sanitizer_flags.inc)을 지정해 결함이 난 Apple sanitizer runtime의 대체 signal-stack 설치·해제 경로만 피했다. 주소·수명·UB 계측과 `halt_on_error=1`은 그대로 유지된다. 다만 실제 stack exhaustion 때 sanitizer signal handler가 별도 stack을 쓰지 못하므로 stack-overflow 보고의 신뢰성은 낮아질 수 있다.

후속 변경 뒤 macOS 26.6/AppleClang 21에서 Debug 13/13(18.60초), Release 13/13(40.57초), `use_sigaltstack=0`을 적용한 ASan+UBSan 13/13(46.38초)이 모두 통과했다. CMake preset parsing과 `git diff --check`도 통과했다. macOS 15 호스팅 runner에서의 우회책 검증은 이 후속 변경이 원격 CI에서 실행될 때까지 남아 있다.

## 직접 검토했으나 finding에서 제외한 항목

- NativeExact 축소가 layer별 scale 때문에 합성 순서를 바꾼다는 후보: opacity/blend/clip/group 조합과 16→5 nearest 축소 probe에서 full native 합성 후 축소와 pixel 차이가 0이었다. 확인된 결함이 아니므로 제외했다.
- 선택 stroke-ID cache 무효화 누락 후보: document 변경 신호와 CanvasWidget/preview invalidation 경로를 추적했으나 확정 stale-cache 경로를 찾지 못했다.
- autosave/복구의 저장 원자성·worker 수명 후보: `QSaveFile` commit/error, pending selection snapshot, cancellation, receiver destruction과 shutdown 순서를 추적하고 관련 document/UI-session suite를 실행했으나 추가 확정 결함은 없었다.
- mask stride/tail bit, affine 경계, layer hierarchy cycle/clip 불변식, history delta 순서, raster cache 동기화, GIF/WebP 오류 전파도 직접 확인했으나 별도 확정 결함은 찾지 못했다.

## 남은 위험과 한계

다음은 재현된 미수정 결함이 아니라 이번 감사가 완전히 제거하지 못한 범위 또는 모델 한계다.

- Wawa deduplication은 새 content인지 확인하기 위해 retained decoded budget 외에 현재 PNG 한 장의 decode/canonical 작업 backing을 필요로 한다. 최대 크기에서는 포맷 변환 여부에 따라 상당한 transient가 남는다.
- 128 MiB raw JSON 입력 제한은 JSON DOM, Base64, 압축 해제, validation 중 생기는 모든 복사본의 process RSS 상한은 아니다. 개별 distinct mask/raster decoded budget은 적용되지만 전체 load peak를 직접 계측하는 단일 budget은 없다.
- export/preview 정책은 renderer가 소유하는 QImage surface, section, reframe epoch와 명시적 scratch를 보수적으로 계산한다. 문서가 이미 보유한 raster/mask backing, Qt `QPainter`, image codec 내부 allocator까지 포함하는 전체 프로세스 RSS 보장은 아니다.
- bounded read는 `QFile::open()` 뒤의 읽기 양을 제한하지만 FIFO나 특수 디바이스를 경로로 직접 넘길 때 `open()` 자체가 block되는 문제까지 막지는 않는다.
- 최종 동적 검증은 macOS arm64에서 수행했다. Windows 전용 빌드·파일 시스템·UI 경로와 장시간 실제 GPU/디스플레이 상호작용은 이번 실행 환경에서 재현하지 않았다.
- 디스크 가득 참, 갑작스러운 프로세스 종료, 전원 손실 같은 실제 장애를 조합한 장시간 recovery fault injection은 수행하지 않았다. 관련 원자적 저장과 오류 경로는 코드 검토 및 기존 lifecycle/session 테스트로 확인했다.

## 결론

기준 커밋에서 확정한 12개 결함은 모두 최소 범위로 수정했고, 각 재현을 회귀 테스트로 남겼다. 최종 Debug/Release/sanitizer/static-analysis/package 검증은 전부 통과했다. 위 남은 위험을 제외하면 이번 감사 범위에서 재현 가능한 미수정 결함은 없다.
