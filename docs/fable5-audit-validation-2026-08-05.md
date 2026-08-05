# Sol Ultra 감사 보고서 독립 검증 — Fable 5

- 검증일: 2026-08-05
- 대상 보고서: [code-audit-report-2026-08-05.md](code-audit-report-2026-08-05.md)
- 검증 기준 커밋: `ee063ae` (main, working tree clean)
- 검증자 주의: 감사 보고서는 `3be2634` 시점 트리를 기준으로 작성되었고, 그 뒤 `ee063ae` 한 커밋(레이어 패널 폭 + `ui` 테스트 suite 5분할)이 추가되었다. 보고서의 "9개 suite / CTest 9/9"는 해당 시점 기준으로 정확하며, 현재 HEAD는 13개 suite다.

## 1. Executive summary

**전반적 신뢰도: 높음.** 15개 위험 항목(R-01~R-15)을 전수 독립 검증한 결과, 위치·메커니즘·발생 조건이 실제 코드와 일치하지 않는 항목은 1건(R-14의 영향 판단)뿐이었다. High 5개 중 3개(R-01, R-02, R-03)는 코드 추적과 수치 대조로 결함이 확정되었고, 2개(R-04, R-05)는 구조적 사실은 맞지만 심각도가 과장되었다.

- **Confirmed defect: 7건** — R-01(export 예산 우회), R-02(autosave 완료 신원 오귀속), R-03(live input의 history 경계 누수), R-07(move-mode arm flag 누수), R-09(wobble override export 불일치), R-10(lasso rollback snapshot), R-13(주석-구현 모순, 문서 결함)
- **Probable defect: 2건** — R-08(visibility 작업 증폭, 구조 확정·영향 미계측), N-01(신규: GUI 슬롯의 `QFuture::result()` 예외 재던짐)
- **오탐 또는 과장: 5건** — R-14(영향 없음: 보상 메커니즘 존재), R-04(hang leg 도달 불가), R-05(현행 종료 경로에서 도달 경로 없음), R-08(메모리 규모 과장: CoW), R-09(`frame>0` 한정 오류·eyedropper leg 축소)
- **신규 발견: 4건** — N-01(예외 재던짐 3개소), N-02(종료 시 detached pool task vs `RasterAssetCache` static 파괴 순서), N-03(테스트 인프라 검증 공백), N-04(frame scrub 중 stroke-property action 미갱신)
- **즉시 수정 가능 여부:** 상위 항목별 실패 재현 테스트를 먼저 추가하는 조건으로 수정 착수 가능. 세부 판단은 §10.

보고서가 "잘 방어됨"으로 분류한 6개 영역(frame warmup, LayerDock thumbnail, worker 값 snapshot, `RasterAssetCache` mutex, selection 완료 gating, replacement 원자성)은 모두 재검증에서 **사실로 확인**되었다. 단 frame warmup·thumbnail·selection의 완료 핸들러가 공유하는 `result()` 예외 경계 부재(N-01)와 종료 시점 static 파괴 순서(N-02)는 보고서가 놓친 잔여 위험이다.

## 2. Audit methodology

**조사한 파일·서브시스템** (전부 정독, 발췌가 아닌 경로 추적):

- Export: `MainWindowExport.cpp`, `GifExportDialog.cpp`, `ExportWorker.{hpp,cpp}`, `AnimationExportPolicy.{hpp,cpp}`, `RenderExportPolicy.cpp`, `MemoryBudget.hpp`, `RenderEngine.cpp`, `LayerHierarchyCompositor.cpp`, `LayerCompositionPlan.*`
- Autosave/recovery: `RecoveryWriter.{hpp,cpp}`, `MainWindow.cpp`(autosave·close·recovery 전 경로), `RecoveryStore` 사용부
- Live input/selection: `CanvasWidget.{hpp,cpp}`, `CanvasWidgetTools.cpp`, `CanvasWidgetEvents.cpp`, `CanvasWidgetSelection.cpp`, `CanvasWidgetPreview.cpp`, `SelectionVisibility.cpp`, `MainWindowActions.cpp`(undo/redo·selection action), `FileOpenEventRouter.cpp`
- History: `DocumentController.cpp`(상태 설치·effect dispatch·addStroke), `DocumentControllerStrokes.cpp`, `DocumentUndoStack.cpp`, `HistoryEffects.hpp`
- 수명: `MainWindow.hpp` 멤버 순서, `TimelineBar`/`WobblePreview`/`FrameScrubber`/`LayerDock` 파괴자·비동기 경로, `UpdateControllerMac.mm`, `RasterAssetCache.cpp`, `main.cpp`
- 빌드·테스트: `CMakePresets.json`, `UguruguBuildSettings.cmake`, `UguruguTests.cmake`, `.github/workflows/ci.yml`, `TestMain.cpp`, `GifWriterTests.cpp`, `UiSessionTests.cpp`, `UiSelectionTests.cpp`, `DocumentLifecycleTests.cpp`, `LayerSplitPreviewTests.cpp:510-531`, `UiViewportTests.cpp:420-444`

**실행한 빌드·테스트** (모두 이번 검증에서 직접 실행):

|구성|결과|
|---|---|
|macos-debug|configure/build 성공, CTest **13/13 통과**, 37.93초|
|macos-release|build 성공, CTest **13/13 통과**, 53.83초|
|macos-sanitized (ASan+UBSan, `halt_on_error=1`)|build 성공, CTest **13/13 통과**, 101.14초, 보고 0건|

**사용한 방법:** 정적 경로 추적(생성자/파괴자/소유권/signal 연결 방식/thread affinity), 기존 테스트가 고정한 수치와의 대조(예: `GifWriterTests.cpp:92-112`의 896 MiB), 3개 sanitizer/일반 구성 실행. **소스는 일절 수정하지 않았다.**

**검증하지 못한 부분(한계):** native GUI 이벤트 순서(전 테스트 offscreen), TSan(구성 자체가 없음), OOM·예외 주입 실행(코드 수정 금지 제약), N-02의 종료 시점 재현, R-08의 자원 사용 계측. 이들 항목의 판정은 정적 분석에 근거하며 본문에 그 한계를 명시했다.

## 3. Finding validation matrix

|ID|원래 주장|최종 분류|심각도|신뢰도|핵심 근거|
|--|-----|-----|---|---|-----|
|V-01 (R-01)|애니메이션 export UI가 size-only 예산 추정 사용|**Confirmed defect**|High(정책)/Medium(crash)|높음|`MainWindowExport.cpp:44`·`GifExportDialog.cpp:101,120` 모두 size-only overload. document-aware overload는 테스트에서만 사용. 100% export는 `renderAtSize`가 native 크기로 렌더(`LayerHierarchyCompositor.cpp:92-143`). 896 vs 512 MiB는 `GifWriterTests.cpp:107-112`가 고정한 수치와 일치|
|V-02 (R-02)|오래된 autosave A 성공이 B의 pending을 해제|**Confirmed defect**|High|높음|`handleAutosaveWritten`(`MainWindow.cpp:1322-1338`)이 완료 신원 없이 `m_autosaveEditGeneration == m_submittedEditGeneration`만 비교. signal이 나르는 `revision`은 미사용|
|V-03 (R-03)|문서 교체/Undo/Redo가 live stroke·lasso를 취소하지 않음|**Confirmed defect** (undo/redo leg), Probable (교체 leg)|High|높음|`documentChanged`/`documentReplaced` 연결(`CanvasWidget.cpp:64-78`)이 stroke/lasso 미취소. undo/redo 단축키는 드로잉 중 활성(`MainWindowActions.cpp:206-253`). `addStroke`는 동일 layer UUID면 수용 → redo tail 소거|
|V-04 (R-04)|worker 예외 경계 부재 → terminate 또는 영구 busy|**Plausible risk** (hang leg는 오판)|Medium|높음|`ExportWorker::process`/`RecoveryWriter::processPending`에 try/catch 없음(사실). 단 저장소에 `throw`문 0개, 현실적 예외원은 컨테이너 `bad_alloc`뿐이고 결과는 terminate(즉사) — busy 잔류 hang은 도달 불가|
|V-05 (R-05)|controller-first 파괴 후 raw observer UAF|**Plausible risk** (latent 계약 위험)|Medium(잠재)|높음|멤버 순서상 controller가 child widget보다 먼저 파괴되는 것은 사실(`MainWindow.hpp:124,177-178`). 그러나 파괴 중 이벤트 루프가 돌지 않고 `~CanvasWidget`(warmup cancel만)·`~TimelineBar`(eventFilter 해제만)는 controller 미접근 → 현행 코드에 도달 경로 없음|
|V-06 (R-06)|종료가 현재 render/serialization을 무제한 대기|**Confirmed(구조)/Plausible(증상)**|Medium|높음|양쪽 destructor 모두 `waitForIdle(Forever)`(`ExportWorker.cpp:53-59`, `RecoveryWriter.cpp:23-33`). export cancel은 frame 단위 체크로 유계, recovery destructor는 의도적으로 마지막 write를 flush. hang은 단일 작업이 끝나지 않을 때만|
|V-07 (R-07)|copy arm flag가 async generation에 미귀속|**Confirmed defect**|Low-Medium|높음|stale 완료가 flag를 남긴 채 반환(`CanvasWidgetSelection.cpp:345-350`), 다음 매칭 완료가 `std::exchange`로 소비(`:370-374`). `clearSelection`(`:837-858`)은 flag 미리셋|
|V-08 (R-08)|visibility 작업이 취소되지 않고 증폭|**Probable defect**|Medium|높음(구조)/낮음(규모)|`QtConcurrent::run` 전역 pool, 취소 토큰 없음, 모든 `documentChanged`마다 `pruneSelection`→새 작업, in-flight 억제·debounce·사전 캐시 조회 없음. 단 Document snapshot은 CoW라 메모리 증폭은 divergence 비례(보고서 과장)|
|V-09 (R-09)|wobble-off에서 layer override가 export·eyedropper에 잔존|**Confirmed defect** (export), 축소 (eyedropper)|Medium|높음|`displayDocument`(`CanvasWidget.cpp:242-257`)는 문서+레이어 정규화, `exportImage`(`MainWindowExport.cpp:102-105`)는 문서 레벨만. eyedropper는 pending transform 중일 때만 결함(평시 `displayDocument` 사용). wobble은 frame 0에서도 변위하므로 "frame>0" 한정은 불필요|
|V-10 (R-10)|lasso rollback snapshot 미변환·`setLassoMode` 미복원|**Confirmed defect**|Low-Medium|높음|`handleCanvasResized`(`CanvasWidgetSelection.cpp:1025-1057`)가 live mask·lasso 좌표는 변환하되 `m_selectionBeforeArea` 미변환. `setLassoMode`(`CanvasWidget.cpp:1081-1094`)는 복원 없이 `cancelAreaSelection`. 결과는 corruption이 아닌 **selection 유실**(`restoreSelectionState:293` size guard가 강등 처리)|
|V-11 (R-11)|worker context QObject의 off-affinity 파괴|**Plausible risk** (hygiene)|Low|높음|사실이나 thread quit+wait 후·timer/child/pending 이벤트 없음 → 현행 동시성 결함 없음. 보고서 스스로 crash를 가설로 분류(정확)|
|V-12 (R-12)|history signal snapshot 재진입 시 stale 재발행|**Plausible risk** (latent)|Low|높음|패턴 사실(`DocumentUndoStack.cpp:257-287`, `m_moving`이 notify 전 해제). 단 production 유일 소비자 `syncHistoryActions`는 payload 무시·live 재조회·비변이 → 현행 트리거 없음|
|V-13 (R-13)|HistoryEffects 주석이 구현과 모순|**Confirmed** (문서 결함)|Low|높음|`HistoryEffects.hpp:24-26` "이전 문서가 current" vs `DocumentController.cpp:1360-1367` target 설치 후 dispatch(의도적, 인접 주석이 명시). 순서를 고정하는 테스트 없음|
|V-14 (R-14)|frame shrink 시 signal 누락으로 stale observer/action|**False positive** (영향 기준)|Low(취약성만)|높음|전제는 사실(`CanvasWidgetPreview.cpp:455-456` 무통지 정규화). 그러나 canvas가 첫 연결이라 정규화가 소비자 resync보다 선행하고, `pruneSelection`이 availability 재발행 → stale 상태가 실체화되지 않음. connection order 의존은 취약성으로 기록|
|V-15 (R-15)|자동 검증 전부 offscreen·TSan 부재|**Confirmed**|Medium(공백)|확정|`UguruguTests.cmake:54` offscreen 강제, sanitizer는 address+undefined만(`UguruguBuildSettings.cmake:71`), CI에 TSan row 없음|

신규 발견 N-01~N-04는 §6.

## 4. Confirmed and probable defects

### V-01 (R-01) — 애니메이션 export 메모리 예산 우회

**실행 경로:** `MainWindow::exportAnimation`(`MainWindowExport.cpp:30`)은 입구에서 ¼ 크기 size-only 검사만 수행(`:44-47`)하고, `GifExportDialog`는 각 scale 항목과 OK 버튼을 `fitsMemoryBudget(size, frames)`(size-only, `GifExportDialog.cpp:101,120`)로만 gating한다. size-only 추정은 `w×h×frames×12B`(`AnimationExportPolicy.cpp:9-19`) — hierarchy 항이 없다. 반면 worker의 실제 렌더는:

- 100% export: `RenderEngine::render` → `renderAtSize(document, frame, document.size)` — hierarchy 합성 surface 전부 native 크기. 깊은 group/clipping 문서에서 peak = `m_peakSurfaceCount × native surface`.
- 축소 export: `renderAtSize`가 **각 paint layer를 native 크기로 렌더 후 축소**(`LayerHierarchyCompositor.cpp:116-134`) — scale과 무관하게 native 크기 transient가 layer마다 발생.

**수치 대조(테스트가 고정):** 4096×4096·2-frame 문서에서 size-only 추정 = 384 MiB ≤ 512 MiB(`MemoryBudget.hpp:21`) → dialog가 100%를 허용. 동일 문서의 deep fixture(`GifWriterTests.cpp:24-62`, 9단 중첩+clip)는 document-aware 추정 896 MiB(`GifWriterTests.cpp:107-109`)로 예산의 175%. document-aware overload(`AnimationExportPolicy::fitsMemoryBudget(const Document&)`)의 **호출자는 테스트뿐**이다. 정적 이미지 export는 document-aware 검사를 사용(`MainWindowExport.cpp:109`)하므로 애니메이션 경로만 정책에서 이탈해 있다 — 비대칭 자체가 의도 위반의 증거다.

**증상 정정:** 896 MiB는 현대 macOS에서 대개 할당에 성공하므로 즉각적 `bad_alloc`/terminate보다는 **예산 정책 위반과 메모리 압박**이 1차 증상이고, crash는 저사양·대형 문서 조건부다. 보고서의 crash 강조는 다소 과장이나 결함 자체는 확정.

### V-02 (R-02) — autosave 완료 신원 오귀속

**실행 경로:** ① 편집 → `documentChanged` → `m_autosavePending=true; ++m_autosaveEditGeneration`(`MainWindow.cpp:722-729`). ② `writeAutosave`(30초 타이머 또는 deactivate, `:1277-1320`)가 A(revision n)를 submit하고 `m_submittedEditGeneration`을 현재 generation으로 갱신. ③ worker가 A를 집필 중(active), 새 편집 후 `writeAutosave`가 B(revision n+1)를 submit — `m_submittedEditGeneration`이 B의 generation으로 **덮어써진다**. ④ A 성공 → `writeFinished(true, n)` → `handleAutosaveWritten`(`:1322-1338`)은 "**어느 write가** 끝났는지" 모른 채 `m_autosaveEditGeneration == m_submittedEditGeneration`(B 기준, 일치)을 보고 `m_autosavePending=false`. ⑤ B 실패 → 로그만 남기고 반환 — **pending이 이미 꺼져 있어 다음 편집까지 재시도가 없다**. 그 사이 crash가 나면 recovery 파일에는 A 시점 내용만 있다.

**반증 시도:** `RecoveryWriter::writeFinished`는 이미 `revision`을 전달한다(`RecoveryWriter.hpp:41`) — 즉 신원 정보는 존재하나 소비 측이 사용하지 않는다. worker는 단일 스레드 FIFO이므로 순서 역전은 없다. B가 성공하는 일반 경로에서는 무해 — 결함은 "A 성공 + B 실패" 조합에서만 사용자에게 도달한다. 발생 조건이 좁아 재현성은 낮지만 crash-recovery의 핵심 보증을 깨므로 High 유지.

**최소 수정 방향(§8.2):** `handleAutosaveWritten`에서 `revision == m_submittedRecoveryRevision`도 함께 요구. UI 한 곳 수정으로 충분하다.

### V-03 (R-03) — live input이 문서/history 경계를 넘음

**실행 경로(undo/redo leg, 확정):** `beginStroke`(`CanvasWidgetTools.cpp:23`)는 `m_activeStroke`/`m_activeStrokeLayer`를 잡고, 취소는 FocusOut/UngrabMouse/TabletLeaveProximity/WindowDeactivate/ApplicationDeactivate와 resize dialog 경로에서만 일어난다(`CanvasWidgetEvents.cpp:60-71`, `MainWindow.cpp:705-715,1020,1065`). `documentChanged` 핸들러(`CanvasWidget.cpp:64-74`)는 selection transform만 취소하고 **live stroke·active lasso는 남긴다**. undo/redo 액션은 stack 상태로만 enable되고 단축키는 드로잉 중에도 동작한다(`MainWindowActions.cpp:206-253`). 따라서 press 중 Cmd+Z → release 시 `addStroke`가 **undo된 상태의 동일 layer UUID**에 커밋 → push가 redo tail을 소거하고 stroke는 과거 상태 위에 합성된다. resize를 undo한 경우에는 begin 시점에 캡처한 `clipMask` 크기가 현재 문서와 어긋나 `RejectedInvalidStroke`(`DocumentControllerStrokes.cpp:146-148`) → **입력 유실**. lasso도 동일하게 경계에서 살아남는다(별도 검증: `pruneSelection`/`clearSelection` 모두 `m_areaSelection*` 미접촉).

**교체 leg(Probable):** 앱 내 교체 경로(Open/New/recovery)는 전부 modal dialog를 경유하므로 WindowDeactivate가 입력을 취소한다. 남는 경로는 macOS `QFileOpenEvent`(`FileOpenEventRouter.cpp:51-67` — `activeModalWidget`만 검사, live input 미검사)로, 같은 파일을 다시 열면 직렬화된 layer UUID가 일치해 stroke가 새로 로드된 문서에 커밋될 수 있다. 도달 가능하나 조건이 특수해 leg별로는 Probable.

**반증 시도:** `addStroke`의 검증(layer 존재·mask 크기·point 범위)이 많은 오커밋을 거부하는 것은 사실이나, **undo/redo는 UUID·크기가 그대로**이므로 방어가 작동하지 않는다. segfault 아님(보고서 판단과 일치) — 상태·history 결함.

### V-07 (R-07) — copy arm flag가 세대에 귀속되지 않음

`copySelection`이 flag를 세팅(`CanvasWidget.cpp:572`)한 뒤, stale visibility 완료는 flag를 남긴 채 반환(`CanvasWidgetSelection.cpp:345-350`)하고 `clearSelection`도 리셋하지 않는다(`:837-858`). 이후 **무관한 새 selection**의 평가 완료가 `std::exchange`(`:370`)로 leaked flag를 소비해 예고 없이 move mode로 진입한다. 트리거: Copy 직후(완료 콜백 도착 전) layer 전환·문서 교체·deselect → 새 selection. 대형 animated 문서일수록 창이 넓다. Undo로 복구 가능한 UX 결함 — Low-Medium. 수정은 `clearSelection`과 stale-return에서 flag 리셋(2줄 수준).

### V-08 (R-08) — visibility 작업 증폭 (Probable)

구조는 전부 확정: 전역 `QThreadPool`에 `QtConcurrent::run`(`CanvasWidgetSelection.cpp:382-389`), 취소 토큰 없음(같은 파일의 warmup은 atomic 취소를 갖고 있어 패턴 부재가 대비됨), **모든** `documentChanged`가 `pruneSelection` 경유로 새 작업을 발주하고, in-flight 억제·debounce가 없으며, controller 결과 캐시는 발주 전에 조회되지 않는다(`DocumentController.cpp:442-456`은 쓰기 전용 소비). 작업당 최악 비용은 `animationFrames`(기본 30) frame 렌더. **정정:** Document snapshot은 implicit sharing이라 메모리 증폭은 "작업 수 × 문서 크기"가 아니라 세대 간 divergence에 비례한다 — 보고서의 RSS 성장 서술은 과장. CPU/latency 증폭과 pool 기아는 구조적으로 성립. 영향 계측 전이므로 Probable.

### V-09 (R-09) — wobble override의 export 불일치

`displayDocument()`는 wobble off일 때 문서 `wobbleAmount`와 **모든 layer override**를 0으로 정규화한다(`CanvasWidget.cpp:242-257`). `exportImage`는 raw 문서에 문서 레벨만 0을 넣는다(`MainWindowExport.cpp:102-105`) — layer override(`Document.hpp:224`, renderer가 `value_or`로 우선 적용)가 살아남아 **화면(무변위)과 export 파일(변위)이 다른 픽셀**을 만든다. eyedropper는 평시 `displayDocument()`를 쓰므로 안전하고(보고서가 놓친 방어), pending selection transform 중에만 같은 결함을 재현한다(`CanvasWidgetTools.cpp:437-443`). wobble 변위는 frame 0에서도 발생하므로 보고서의 "frame>0" 조건은 불필요 — 결함 범위는 오히려 더 넓다. 수정은 export 경로에서 display와 동일한 정규화 사용(수 줄).

### V-10 (R-10) — lasso rollback snapshot

두 하위 주장 모두 코드와 일치: `handleCanvasResized`는 live mask와 lasso 좌표를 변환하되 `m_selectionBeforeArea`는 방치하고, `setLassoMode`는 sibling(`setSelectionShape`, `setTool`)과 달리 복원 없이 snapshot을 버린다. **정정(완화):** `restoreSelectionState:293`의 size/layer guard가 stale snapshot 설치를 막아 결과는 corruption이 아닌 **이전 selection의 무통지 유실**이다. 추가 발견: resize-undo 후 release로 lasso를 완성하면 stale 크기의 before-mask가 history command에 포장되어, 그 command를 undo할 때 복원 대신 deselect로 강등된다(`pushSelectionStateCommand`는 size 검증을 하지 않음).

### V-13 (R-13) — 주석-구현 모순 (문서 결함)

`HistoryEffects.hpp:24-26`은 before-event가 "이전 문서가 current일 때" 실행된다고 하나, 구현은 target 상태를 먼저 설치한 뒤 dispatch한다(`DocumentController.cpp:1360-1367`) — 인접 주석("Install the complete observable controller state before any callback")이 이 순서가 의도임을 명시한다. 모든 경로(`redo`/`undo`/`TransientCommand`)가 동일하며, 소비자는 signal payload만 읽으므로 행위 결함은 없다. 수정은 헤더 주석 교정 + 순서 고정 테스트.

### N-01 — GUI 슬롯의 `QFuture::result()` 예외 재던짐 (Probable, 신규)

§6 참조.

## 5. False positives and overstated risks

- **V-14 (R-14) — False positive(영향 기준).** 전제(무통지 `m_currentFrame %=`, action의 frame signal 미구독)는 정확하나, ① canvas가 `documentChanged`의 첫 연결이라 정규화가 TimelineBar/FrameScrubber의 resync(둘 다 live 재조회)보다 항상 선행하고, ② 같은 신호에서 `pruneSelection`이 `selectionAvailabilityChanged`를 재발행해 action이 정규화된 frame으로 재평가되며, ③ frame-키 캐시는 `invalidateFrames`가 리셋한다. connection order 의존이라는 **취약성**은 실재하므로 문서화·테스트 가치는 있으나 현행 stale 상태는 실체화되지 않는다.
- **V-04 (R-04) — hang leg 과장.** 저장소에 `throw`문이 없고 QImage 할당 실패는 null 반환이다. 현실적 예외원은 Qt/STL 컨테이너의 `bad_alloc`뿐이며, 이는 worker 이벤트 경계를 빠져나가 **즉시 terminate**한다 — "busy 잔류 후 destructor hang"에 도달하려면 예외가 어딘가에서 삼켜져야 하는데 그런 코드가 없다. 예외 경계 추가는 유효한 위생 조치지만 High가 아닌 Medium-Low.
- **V-05 (R-05) — 심각도 과장.** 파괴 순서 사실은 확인되나, ~MainWindow 전 구간에서 이벤트 루프가 돌지 않고(worker 대기는 `QWaitCondition`), controller를 읽는 child 파괴자가 없으며, 비동기 완료는 전부 `this` context 연결이라 파괴와 함께 절단된다. `WobblePreview::paintEvent`(`WobblePreview.cpp:86`)의 controller 접근은 위험 표면이지만 **도달시키는 이벤트가 없다**. "API misuse 시 UAF"라는 잠재 계약 위험으로 유지하되 현행 결함 아님 — 보고서 5.2도 이를 인정하고 있어, 위험 등급 High는 register 표기 기준으로 과대.
- **V-08 (R-08) — 메모리 규모 과장.** Document 복사는 CoW(§4). CPU 증폭·pool 점유는 유효.
- **V-09 (R-09) — 트리거 서술 부정확.** eyedropper leg는 pending transform 중으로 한정되고, "frame>0" 한정은 불필요(frame 0에서도 변위).
- **V-01 (R-01) — crash 확률 과장.** 결함은 확정이나 1차 증상은 정책 위반/메모리 압박(§4).
- **V-12 (R-12) — Medium→Low.** production 소비자가 payload-agnostic·비변이라 현행 트리거 없음. `m_moving`이 notify 전에 해제되는 구조는 사실이므로 방어적 테스트 가치만 있음.

보고서가 "방어됨"으로 서술한 항목의 재검증 결과(모두 확인): frame warmup은 `shared_ptr<const Document>`+atomic 취소+generation/identity/size 3중 gate(`CanvasWidgetPreview.cpp:570-585`), LayerDock thumbnail은 `QPointer` GUI-thread 검사+revision(`LayerDock.cpp:960-965`), worker는 값 snapshot만 보유, `RasterAssetCache`는 전 공개 진입점 lock+CoW 반환, 문서 교체 재진입은 `QScopedValueRollback` guard, `m_currentState`는 callback 전 설치.

## 6. Newly discovered risks

|ID|위치|내용|분류|
|--|---|---|---|
|N-01|`CanvasWidgetPreview.cpp:570`, `LayerDock.cpp:960`, `CanvasWidgetSelection.cpp:343`|QtConcurrent는 worker 예외를 저장했다가 `result()`에서 재던진다. 세 finished 핸들러 모두 try/catch 없이 `result()`를 호출하며 worker는 렌더 코드(컨테이너 할당 실패 시 `bad_alloc` 가능)를 실행한다. 예외는 이벤트 dispatch 중 슬롯에서 전파되어 macOS에서 terminate. `main.cpp:253`의 catch는 이 경로를 구제하지 못한다. R-04와 같은 계열의 **미보고 지점** — 동일 패턴으로 일괄 수정 가능. (부수: `UpdateControllerWindows.cpp`는 download 핸들러만 try/catch, check 핸들러(:112)는 미보호)|**Probable defect** (crash 경로, 낮은 확률)|
|N-02|`RasterAssetCache.cpp:19-36` + detached pool task|warmup/thumbnail/visibility 작업은 join 없이 detach된다(`~CanvasWidget`은 cancel flag만). `main()` 반환 후 전역 QThreadPool destructor가 대기하는 동안, function-local static인 cache mutex/QCache는 (pool보다 늦게 생성되므로) **먼저 파괴**된다. raster asset 문서를 렌더 중인 작업이 종료 시점에 살아 있으면 파괴된 static 접근 — 종료 시 한정 UAF/segfault 후보. offscreen CI·ASan 통과가 부재 증명이 되지 않는 전형 사례|**Plausible risk** (runtime 재현 필요)|
|N-03|`tests/support/MainWindowTestAccess.hpp:44-63`, `TestMain.cpp:105-113`|테스트가 `m_autosavePending`을 직접 세팅해 실제 `documentChanged` 배선 경로가 미검증. UI suite들이 한 프로세스를 공유해 이전 suite의 detached 작업이 다음 suite 중 실행될 수 있고, N-02의 teardown 순서는 어떤 테스트도 다루지 않음|**검증 공백**|
|N-04|`CanvasWidget.cpp:1201-1214` + `MainWindowActions.cpp:485-524`|`setCurrentFrame`(scrub)은 `syncSelectionActions`를 재실행시키지 않아, live selection 상태로 frame을 이동해도 stroke-property 계열 action의 enable 상태가 frame 의존 편집 가능성을 따라가지 않음 (R-14 검증 중 발견한 인접 공백)|**Probable defect** (UI 상태, Low)|

## 7. Critical invariants

|클래스 또는 서브시스템|유지해야 하는 불변식|위반 경로|예상 증상|
|------------|-----------|-----|-----|
|`MainWindow` autosave|`m_autosavePending`은 **현재 submitted revision의 완료 신호**로만 해제|A active·B pending에서 A 성공(V-02)|crash 시 stale recovery, 재시도 누락|
|`CanvasWidget` live input|press~release 동안 동일 document identity·history node를 유지하거나 경계에서 취소|undo/redo 단축키, `QFileOpenEvent` 교체(V-03)|redo tail 소거, 과거 상태 오염, 입력 유실|
|Export 예산|실행 전 추정은 실제 렌더 경로의 피크(native per-layer render + hierarchy surface)를 포함|`GifExportDialog` size-only gating(V-01)|512 MiB 예산 초과 할당|
|Selection async|`m_armSelectionMoveMode`는 그것을 세팅한 evaluation 세대에서만 소비|stale 완료 조기 반환 + `clearSelection` 미리셋(V-07)|무관한 selection이 move mode 진입|
|Area selection rollback|`m_selectionBeforeArea`는 문서 geometry 변화를 따라가거나, 폐기 시 복원 의미를 보존|mid-lasso resize-undo, `setLassoMode`(V-10)|이전 selection 무통지 유실|
|Wobble-off 정규화|display·export·picker가 동일한 (문서+layer) 정규화를 공유|`exportImage` 문서 레벨만 0(V-09)|화면과 export 픽셀 불일치|
|Worker 완료 경계|pool worker의 예외는 GUI 슬롯/이벤트 루프에 도달하기 전에 정리|`result()` 무보호 3개소(N-01), worker try/catch 부재(V-04)|terminate|
|프로세스 종료|pool task는 접근하는 static보다 먼저 종료(join 또는 수명 보장)|detached 렌더 작업 vs cache static(N-02)|종료 시 UAF|
|`MainWindow` teardown|controller보다 오래 사는 raw observer 이벤트 없음(현재는 이벤트 루프 부재로 성립)|향후 파괴 중 event 처리 도입(V-05)|UAF|
|History 계약|effect dispatch 시 current 상태의 의미는 주석·테스트로 고정|`HistoryEffects.hpp` 주석 신뢰(V-13)|향후 수정자의 old/new 반전|

## 8. Prioritized remediation plan

점수(1-5, 높을수록 심함; Fix complexity는 높을수록 복잡):

|순위|ID|Impact|Repro|Confidence|Regression risk|Fix complexity|비고|
|---|--|---|---|---|---|---|---|
|1|V-03|4|4|5|2|2|사용자 일반 조작(펜 드로잉+Cmd+Z)으로 도달, history 무결성 파괴|
|2|V-02|4|2|5|1|1|crash-recovery 보증 파괴, 수정은 UI 1개소|
|3|V-01|3|5|5|2|2|결정적 재현 가능, 정책 위반 확정|
|4|V-09|3|5|5|1|1|export 산출물 오류, 수 줄 수정|
|5|V-07|2|3|5|1|1|2줄 수정, async 테스트 필요|
|6|V-10|2|4|5|1|2|(차순위)|
|7|N-01|3|1|4|1|2|V-04와 동일 패턴으로 일괄 처리 권장|

### 8.1 V-03 — live input의 history 경계

- **재현 조건:** offscreen `QApplication`에서 canvas에 mousePress+mouseMove 주입 → `undoAction->trigger()` → mouseRelease. 문서에 stroke 2개를 먼저 커밋해 redo tail 존재를 만들 것.
- **Instrumentation:** interaction 시작 시 history node/content revision/document identity/layer UUID 기록 (보고서 §9 제안과 동일 — 유효).
- **선행 실패 테스트:** ① press→undo→release 후 `canRedo() == true`가 기대值(현재 false로 실패해야 함), ② press→resize-undo→release 후 입력 거부 메시지가 아닌 취소 동작 검증, ③ press→`documentReplaced`→release 후 새 문서 무오염. UI integration (`ui_drawing_tools`/`ui_viewport` suite).
- **최소 수정 파일:** `CanvasWidget.cpp`(경계 연결), 필요시 `DocumentController`에 history-movement 알림 1개.
- **유지 불변식:** endStroke 자신의 commit이 유발하는 `documentChanged`로 자기 취소가 일어나지 않을 것(m_drawing이 이미 false인 시점이므로 안전하지만 테스트로 고정).
- **패치 방향:** `documentReplaced`와 history movement 경계에서 `cancelStroke()`+lasso 취소를 호출하는 **단일 종료 경로**. 보고서의 구조 제안과 일치.
- **피할 방식:** press 중 undo action을 disable하는 우회(모달 상태 추가로 회귀 표면 확대), addStroke에 세대 검사를 넣는 하층 수정(UI 정책을 core로 누출).
- **수정 후:** `ui_*` 5 suite + document suite, ASan/UBSan 전체. 수동: 태블릿으로 press 중 Cmd+Z/Cmd+O/Cmd+N, mouse와 tablet 각각.
- **롤백 기준:** 기존 focus/deactivate 취소 테스트 또는 selection undo 테스트가 깨지면 즉시 되돌림.

### 8.2 V-02 — autosave 완료 신원

- **재현 조건:** A active 상태에서 B submit 후 A 성공·B 실패. 결정적 재현에는 `performWrite` 내부 barrier seam이 필요(현 `setSuspendedForTesting`은 active 중 정지가 불가).
- **Instrumentation:** `writeFinished`에 이미 있는 `revision`을 활용 — 추가 계측은 테스트 seam(예: 특정 revision을 실패시키는 주입 또는 recovery 경로 쓰기 차단)만.
- **선행 실패 테스트:** A 성공 신호를 B submit 후에 배달시키고 B를 실패시킨 뒤 `m_autosavePending`이 true로 남아 다음 타이머에서 재submit되는지 검증. integration + failure seam.
- **최소 수정 파일:** `MainWindow.cpp`의 `handleAutosaveWritten` 1개소: `revision == m_submittedRecoveryRevision`을 pending 해제 조건에 추가.
- **유지 불변식:** 실패 시 pending 유지(현행), discard/preserve의 revision 리셋 경로(`:1355-1360,1382-1387`)와의 정합.
- **피할 방식:** RecoveryWriter의 큐 구조 변경(단일 pending slot 설계는 건전), generation 체계 재설계.
- **수정 후:** `ui_session` suite 반복 실행 + ASan. 수동: 대형 문서에서 연속 편집 중 앱 비활성화 반복.
- **롤백 기준:** `flushesPendingAutosaveOnWindowTeardown` 등 기존 autosave 테스트 실패 시.

### 8.3 V-01 — export 예산

- **재현 조건:** deep fixture(`GifWriterTests.cpp:24`)를 `GifExportDialog`에 전달 — 현재 100%가 enable되는 것이 결함.
- **선행 실패 테스트:** 동일 fixture로 dialog 생성 시 100%(및 예산 초과 scale)가 비활성화되는지, `exportAnimation` 입구가 worker 시작을 거부하는지. UI integration + 정책 unit.
- **최소 수정 파일:** `AnimationExportPolicy.{hpp,cpp}`(scaled-output 인지 overload: native per-layer transient + hierarchy(output 크기) + 축적 frame + encoder), `GifExportDialog.cpp`, `MainWindowExport.cpp`.
- **유지 불변식:** `GifWriterTests`가 고정한 기존 수치(320/384/896 MiB)를 깨지 않을 것 — 새 overload 추가로 하고 기존 함수 의미는 보존.
- **피할 방식:** 렌더러를 output-size 전용으로 바꾸는 최적화(NativeExact의 픽셀 정확성 계약 침해), 예산 상수 조정.
- **수정 후:** gif/webp/render suite + Release(수치 경로 동일성). 수동: 4096×4096 deep 문서로 실제 export하며 Activity Monitor로 피크 확인.
- **롤백 기준:** shallow 문서의 100% export가 거부되기 시작하면 추정식 오류 — 즉시 롤백.

### 8.4 V-09 — wobble-off export 정규화

- **선행 실패 테스트:** layer override>0 + wobble off 문서에서 `displayDocument()` 렌더와 export 경로 렌더의 픽셀 비교(frame 0에서 이미 달라야 함). render/UI integration.
- **최소 수정 파일:** `MainWindowExport.cpp`의 `exportImage`(정규화를 `displayDocument`와 동일하게), `CanvasWidgetTools.cpp`의 pending-transform eyedropper 분기.
- **패치 방향:** 정규화를 `CanvasWidget`의 단일 helper로 통일(보고서 §9의 공통 정책 제안과 일치하되 최소 범위로).
- **피할 방식:** renderer 쪽에서 wobble off를 처리하는 전역 변경.
- **수정 후:** render + ui_viewport suite. 수동: override layer가 있는 문서를 wobble off로 PNG export해 화면과 대조.
- **롤백 기준:** wobble on export의 픽셀이 변하면 롤백.

### 8.5 V-07 — arm flag 세대 귀속

- **선행 실패 테스트:** Copy → (완료 전) `clearSelection` 또는 layer 전환 → 새 selection → move mode 미진입 검증. async UI 테스트, `QTRY_*` 기반.
- **최소 수정 파일:** `CanvasWidgetSelection.cpp` 2개소(stale 조기 반환 시와 `clearSelection`에서 flag 리셋) 또는 flag를 세대 값으로 대체.
- **유지 불변식:** 정상 Copy→완료 경로의 move mode arm(기존 테스트 `UiSelectionTests.cpp:925-935`).
- **피할 방식:** setSelectionMoveMode 전반의 재설계.
- **수정 후:** ui_selection suite 반복(10회) + ASan. 수동: 대형 animated 문서에서 Copy 직후 빠른 layer 전환.
- **롤백 기준:** 기존 copy/move mode 테스트 실패 시.

**후속(top 5 외):** V-10(lasso snapshot 변환+`setLassoMode` 복원), N-01+V-04(worker/watcher 완료 경계 try/catch를 한 패턴으로), V-13(주석 교정+순서 고정 테스트), V-08(발주 전 캐시 조회+in-flight 억제), N-02(종료 전 pool 대기 또는 cache 수명 보장), V-05(owner-first 파괴 계약 테스트+ASan).

## 9. Regression test plan

각 수정 **전에** 추가할 테스트(실패 상태로 먼저 커밋):

|대상|테스트|종류|Sanitizer|
|---|---|---|---|
|V-03|press→undo/redo→release: redo tail 보존, 취소 대칭성|UI integration (offscreen 이벤트 주입)|ASan/UBSan|
|V-03|press→resize-undo→release: 입력의 명시적 취소(무언 거부 아님)|UI integration|ASan|
|V-03|press→문서 교체(같은 UUID 문서 재로드 포함)→release: 새 문서 무오염|UI integration|ASan|
|V-02|A-성공/B-실패 interleave 후 pending 유지·재시도 발생|integration + failure seam (barrier/실패 주입 seam 신설 필요)|불필요|
|V-02|실제 `documentChanged` 배선 경유의 autosave arming (N-03 공백 해소)|integration|불필요|
|V-01|deep fixture에서 dialog 100% 비활성·worker 시작 거부|UI integration + 정책 unit|보조 ASan|
|V-09|layer override + wobble off에서 display vs export 픽셀 동일성 (frame 0 포함)|render integration|불필요|
|V-07|Copy→clear/전환→새 selection의 move mode 미진입|async UI (`QTRY_*`)|선택|
|V-10|mid-lasso resize-undo→Escape: 이전 selection 복원; `setLassoMode` 중 lasso: snapshot 복원|UI|불필요|
|V-13|before/after effect 시점의 `controller.document()` 상태 고정|core unit|불필요|
|V-05|controller-first 파괴 후 timer/paint 강제 처리 계약 테스트 (API misuse 분리)|lifecycle integration|**필수 ASan**|
|N-01/V-04|render/serializer 실패 주입 후 `finished(false)`·busy 해제·no-terminate|unit + 예외 주입 seam|불필요|
|N-02|종료 직전 warmup/thumbnail 발주 후 정상 종료 (반복 실행)|integration + 반복|ASan|
|V-12|`canUndoChanged` slot 내 재진입 undo의 신호 정합|core unit|불필요|

공통 원칙(보고서 §8과 동일하게 유지): sleep 대신 barrier/failure seam, Debug에서 반복 실행 후 Release·sanitized로 확장, 테스트 통과를 수명 안전성의 증거로 삼지 않는다.

## 10. Final recommendation

**특정 재현 테스트를 먼저 작성해야 함.**

- V-01(정책 unit)·V-07·V-09·V-10·V-13은 기존 seam만으로 실패 테스트를 즉시 작성할 수 있으므로, 테스트 작성 → 최소 패치 순으로 **바로 착수 가능**하다.
- V-02는 pending-해제 오귀속을 결정적으로 재현할 barrier/실패 주입 seam이 없으므로 **소규모 테스트 instrumentation이 선행**되어야 한다(§8.2).
- V-03은 offscreen 이벤트 주입으로 재현 가능하나 취소-vs-커밋의 UX 계약(경계에서 취소가 맞는지, 진행 중 stroke를 커밋 후 이동이 맞는지)을 패치 전에 확정해야 한다.
- 재감사는 불필요하다. Sol Ultra 보고서는 위치·메커니즘 수준에서 신뢰할 수 있으며, 본 문서의 정정(과장 5건, 오탐 1건, 누락 4건)을 반영해 사용하면 된다.
- 보고서의 "건드리지 않는 것이 나은 영역"(PreparedDocument freezer, DocumentDelta, operation epoch, serializer dedup) 판단에 **동의**한다 — 이번 검증에서도 해당 영역의 방어는 상대적으로 강했고, 상위 5개 수정은 모두 그 영역 밖에서 완결된다.
