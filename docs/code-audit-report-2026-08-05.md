# Ugurugu C++/Qt 코드 감사 보고서

- 감사일: 2026-08-05
- 저장소 루트: <code>/Users/nyabi/Documents/Code/Ugurugu</code>
- 대상: 저장소 전체 소스, 테스트, CMake, 문서 및 CI
- 기준: C++23, Qt Widgets

## 1. Executive summary

감사 범위는 저장소 전체 72,096줄의 C++/Objective-C++ 소스와 테스트, CMake, CI, 문서입니다.

- 저장소 루트: <code>/Users/nyabi/Documents/Code/Ugurugu</code>
- 최초·최종 상태: <code>## main...origin/main</code>
- 감사 중 추적된 파일 변경, 신규 보고서 파일, 포맷팅, 소스 패치는 없었습니다.
- 빌드 산출물은 요청된 preset 실행 과정에서만 갱신됐습니다.
- 직접 재현된 세그멘테이션 폴트나 Critical 등급 결함은 없었습니다.
- 그러나 테스트가 통과한 상태에서도 High 위험 5개가 남아 있습니다.

가장 위험한 서브시스템은 다음과 같습니다.

1. <code>CanvasWidget</code>의 입력·selection·floating transform 상태기계와 문서/Undo 경계
2. 애니메이션 export의 메모리 예산 및 비동기 예외 경계
3. autosave 요청·완료 세대 추적
4. <code>DocumentController</code> raw observer를 보유한 UI 객체의 파괴 순서
5. selection visibility·frame warming·thumbnail이 공유하는 비동기 렌더 자원

실행 검증 결과:

|구성|결과|비고|
|---|---|---|
|Debug|configure/build 성공, CTest 9/9 통과, 36.21초|증분 빌드, 실패 없음|
|Release|build 성공, CTest 9/9 통과, 55.15초|최초 configure는 과거 <code>WagleWaglePaint</code> 경로를 가리킨 생성 cache 때문에 실패. 생성된 <code>_deps</code> subbuild cache 3개만 재생성 후 성공. 환경/기존 산출물 문제로 분류|
|ASan+UBSan|build 성공, CTest 9/9 통과, 105.43초|ASan/UBSan 보고 없음|
|TSan|미실행|preset과 CI 구성에 없음|
|실제 native GUI|미실행|CTest 전부 offscreen|

공식 빌드 절차는 [BUILDING.md:53](../BUILDING.md#L53), preset은 [CMakePresets.json:20](../CMakePresets.json#L20)에 정의돼 있습니다. ASan+UBSan 설정은 [UguruguBuildSettings.cmake:67](../cmake/UguruguBuildSettings.cmake#L67), 모든 CTest의 offscreen 강제는 [UguruguTests.cmake:28](../cmake/UguruguTests.cmake#L28)에 있습니다.

현재 코드 수정에 들어갈 수는 있지만, <code>CanvasWidget</code>, history, autosave, worker 수명에 대한 수정은 먼저 아래 P0 회귀 테스트를 확보한 뒤 최소 패치로 제한해야 합니다. 특히 테스트 통과는 native 이벤트 순서, OOM, thread race, 종료 hang의 안전성 증거가 아닙니다.

## 2. Architecture map

|모듈|책임|주요 클래스|의존 대상|변경 위험도|
|---|---|---|---|---|
|<code>src/document</code>|문서·레이어·stroke·ordered framebuffer operation 값 모델, 계층/예산 검증|<code>Document</code>, <code>Layer</code>, <code>Stroke</code>, <code>SelectionOperation</code>, <code>LayerHierarchy</code>|Qt Core/Gui|매우 높음|
|<code>src/document/history</code>, controller|모든 mutation, prepared-state commit, Undo/Redo, UI side-effect journal|<code>DocumentController</code>, <code>DocumentUndoStack</code>, <code>DocumentDelta</code>, <code>HistoryEffects</code>|document, serializer|매우 높음|
|<code>src/io/serializer</code>|schema 검증, load/save, immutable backing, mask/raster asset deduplication|<code>DocumentSerializer</code>, <code>PreparedDocument</code>, <code>DocumentValidation</code>|document|높음|
|<code>src/render</code>|stroke replay, layer hierarchy 합성, preview/split/raster render|<code>RenderEngine</code>, <code>StrokeRenderer</code>, <code>LayerCompositionPlan</code>, <code>RasterAssetCache</code>|document, Qt Gui|높음|
|<code>src/io</code>|GIF/WebP/static export, clipboard, Wawa import|<code>ExportWorker</code>, <code>GifWriter</code>, <code>WebPWriter</code>, <code>SelectionClipboardCodec</code>|document, render|높음|
|<code>src/ui</code>|입력, selection, preview/cache, dock, timeline, 문서 session 조정|<code>CanvasWidget</code>, <code>MainWindow</code>, <code>LayerDock</code>, <code>TimelineBar</code>|controller, render, io, app|매우 높음|
|<code>src/app</code>|recovery, instance lock, update, memory policy|<code>RecoveryWriter</code>, <code>RecoveryStore</code>, <code>UpdateController</code>|serializer, Qt thread/platform API|높음|
|<code>src/brush</code>, <code>src/input</code>|preset과 stroke 안정화|<code>BrushPresetCatalog</code>, <code>EraserPresetCatalog</code>, <code>StrokeStabilizer</code>|document/render|중간|
|테스트·CMake·CI·tools|9개 suite, sanitizer, coverage, clang-tidy, probe 도구|<code>TestMain</code>, QTest suites|전체 모듈|검증 핵심|

CMake는 <code>ugurugu_core</code>에서 Qt Widgets를 의도적으로 배제하고 UI에서만 <code>Qt6::Widgets</code>와 <code>Qt6::Concurrent</code>를 연결합니다. 경계는 [UguruguTargets.cmake:9](../cmake/UguruguTargets.cmake#L9)에서 확인됩니다.

핵심 흐름은 다음과 같습니다.

1. 입력 경로

   mouse/tablet event → [CanvasWidgetEvents.cpp:300](../src/ui/CanvasWidgetEvents.cpp#L300) → <code>beginStroke/continueStroke/endStroke</code> → [CanvasWidgetTools.cpp:23](../src/ui/CanvasWidgetTools.cpp#L23) → <code>DocumentController::addStroke</code> → prepared state/history commit.

2. 문서 변경 경로

   mutation candidate → <code>DocumentSerializer::prepare</code> → <code>DocumentDelta</code>와 immutable backing lease 생성 → <code>DocumentCommand</code> push/redo → <code>m_currentState</code>, history node, content revision 설치 → <code>documentChanged</code> 및 <code>HistoryEffects</code> 전달. 핵심 구현은 [DocumentController.cpp:1171](../src/document/DocumentController.cpp#L1171)과 [DocumentController.cpp:1324](../src/document/DocumentController.cpp#L1324)입니다.

3. 렌더링 경로

   controller document → <code>CanvasWidget::displayDocument</code> → frame/split/raster cache → live stroke 또는 floating <code>PixelSelectionOp</code> preview → <code>RenderEngine</code> hierarchy composition. [CanvasWidgetEvents.cpp:74](../src/ui/CanvasWidgetEvents.cpp#L74), [CanvasWidgetPreview.cpp:77](../src/ui/CanvasWidgetPreview.cpp#L77).

4. Selection 경로

   mask/layer/선택 ID는 <code>CanvasWidget</code>이 소유합니다. 비동기 visibility 판정 후 floating transform이 source mask·outline·operation을 보유하고, Apply 시에만 controller history에 commit됩니다. [CanvasWidgetSelection.cpp:311](../src/ui/CanvasWidgetSelection.cpp#L311), [CanvasWidgetSelection.cpp:714](../src/ui/CanvasWidgetSelection.cpp#L714).

5. 열기·교체

   <code>openFile</code> → parse/import → <code>activateProject</code> → controller가 새 문서를 먼저 prepare → 성공한 경우에만 현재 상태·history·revision 교체. [MainWindow.cpp:265](../src/ui/MainWindow.cpp#L265), [DocumentController.cpp:540](../src/document/DocumentController.cpp#L540).

6. 저장·autosave·export

   일반 저장은 pending floating transform을 먼저 실제 history에 적용합니다. Autosave와 export는 이를 controller에 적용하지 않고 변환이 포함된 <code>Document</code> snapshot을 worker에 전달합니다. [MainWindow.cpp:912](../src/ui/MainWindow.cpp#L912), [MainWindow.cpp:1277](../src/ui/MainWindow.cpp#L1277), [MainWindowExport.cpp:30](../src/ui/MainWindowExport.cpp#L30).

가장 결합도가 높은 데이터 구조는 <code>Layer::strokes</code>입니다. 일반 brush stroke뿐 아니라 <code>PixelSelection</code>, <code>Reframe</code>, <code>Image</code> operation도 같은 ordered sequence에 저장되므로, stroke 편집·resize·selection·serializer·renderer·Undo가 모두 operation epoch와 순서를 공유합니다. [Document.hpp:167](../src/document/Document.hpp#L167).

## 3. Critical invariants

|클래스|유지해야 하는 불변식|위반 가능한 경로|예상 결과|
|---|---|---|---|
|<code>Document</code>, <code>Layer</code>, <code>Stroke</code>|ID 전역 유일, parent는 존재하는 Group, cycle 없음, active layer 유효, operation payload 상호배타, Reframe epoch 연결, mask/asset/예산 유효|검증을 우회한 직접 값 변경, 잘못된 serializer·delta 변경|잘못된 framebuffer replay, 저장 거부, 메모리 초과|
|<code>PreparedDocument</code>|항상 valid하고 backing은 immutable하며 compact size와 직렬화 내용이 일치|freeze/reuse/lease 로직 변경|Undo 복원 실패, backing UAF, history memory 오계산|
|<code>DocumentController</code>|<code>m_currentState</code> non-null, state/history node/content revision이 callback 전에 함께 설치됨|재진입 commit, 부분 교체, prepare 실패 후 상태 변경|UI/document divergence, <code>requireReady</code>의 <code>qFatal</code>|
|<code>DocumentUndoStack</code>|<code>0 ≤ index ≤ entries.size</code>, command edge node와 controller node 일치, movement 전 target preflight 완료|동기 signal 재진입, open macro 중 save/replace, command merge 오류|stale action, redo tail 손실, history abort|
|<code>CanvasWidget</code> live input|press부터 release까지 동일 document identity·layer·canvas geometry를 사용하거나 경계에서 취소|문서 교체 또는 Undo/Redo가 live stroke/lasso를 취소하지 않음|새 문서 오염, 입력 유실, 잘못된 history branch|
|Selection/floating state|mask 크기와 현재 document 크기 일치, layer 존재, transform session의 source snapshot 보존|resize/Undo 중 lasso, stale async arm, 비대칭 cancel|selection 유실, 다음 selection이 예상 밖 move mode 진입|
|Render/cache state|cache가 정확한 document/frame/render size/wobble generation에만 대응|frame shrink, <code>documentChanged</code> 누락, stale callback 적용|오래된 preview, frame/action 불일치|
|<code>MainWindow</code> session|path·saved revision·recovery ownership·현재 document가 같은 session을 나타냄|autosave 완료 신원 혼동, 교체 중 live input|복구 파일 stale, 새 문서에 이전 입력 유입|
|<code>RecoveryWriter</code>|최신 edit가 성공하기 전 pending 해제 금지, exclusive generation 이전 write는 commit 금지|A/B 완료 신원 미전달|최신 recovery 재시도 누락|
|<code>ExportWorker</code>|한 request만 활성, cancellation 후 bounded 종료, 모든 실패가 busy 해제와 signal로 귀결|비취소 렌더, top-level 예외 경계 부재|종료 hang, terminate, partial UX state|
|UI observer 수명|raw controller observer보다 controller가 오래 살아야 함|controller-first 파괴 후 timer/paint/input event|dangling pointer/UAF 가설|

문서 검증의 실제 경계는 [DocumentOperations.cpp:97](../src/document/DocumentOperations.cpp#L97), [LayerHierarchy.cpp:85](../src/document/LayerHierarchy.cpp#L85), [DocumentValidation.cpp:150](../src/io/serializer/DocumentValidation.cpp#L150)에 집중돼 있습니다.

## 4. Risk register

신뢰도 기준:

- <code>Confirmed</code>: 코드·기존 테스트 수치 또는 실제 실행 결과로 구조적 사실을 확인
- <code>High confidence</code>: 실행 가능한 interleaving을 코드로 추적했으나 결정적 회귀 테스트는 없음
- <code>Hypothesis</code>: 예외 주입, 특수 파괴 순서, 플랫폼 이벤트가 추가로 필요

Critical은 발견하지 못했습니다.

|ID|심각도|신뢰도|위치|위험 내용|발생 조건|예상 증상|검증 방법|
|---|---|---|---|---|---|---|---|
|R-01|High|Confirmed|[MainWindowExport.cpp:42](../src/ui/MainWindowExport.cpp#L42), [GifExportDialog.cpp:94](../src/ui/GifExportDialog.cpp#L94)|애니메이션 UI가 document-aware 메모리 추정을 버리고 size-only overload 사용|깊은 group/clipping 문서 export|예산 초과 할당, <code>bad_alloc</code>, terminate 또는 OS kill|기존 deep fixture를 실제 dialog까지 연결|
|R-02|High|High confidence|[MainWindow.cpp:1277](../src/ui/MainWindow.cpp#L1277), [RecoveryWriter.cpp:100](../src/app/RecoveryWriter.cpp#L100)|오래된 autosave A 완료가 최신 B의 edit generation으로 판정됨|A active, B pending, A 성공 뒤 B 실패|최신 편집 recovery 재시도 누락|요청별 generation과 B 실패를 강제|
|R-03|High|High confidence|[CanvasWidget.cpp:64](../src/ui/CanvasWidget.cpp#L64), [CanvasWidgetTools.cpp:23](../src/ui/CanvasWidgetTools.cpp#L23)|문서 교체/Undo/Redo가 live stroke와 active lasso를 취소하지 않음|press와 release 사이 open/new/history movement|새 문서 오염, redo tail 삭제, 입력 유실|same layer UUID·resize를 포함한 event-level test|
|R-04|High|Hypothesis|[ExportWorker.cpp:130](../src/io/ExportWorker.cpp#L130), [RecoveryWriter.cpp:100](../src/app/RecoveryWriter.cpp#L100)|worker event callback에 top-level 예외 정리 경계가 없음|렌더/직렬화가 예외 발생|<code>std::terminate</code> 또는 busy 유지 후 destructor hang|예외 injection, terminate/throw breakpoint|
|R-05|High|Hypothesis|[MainWindow.hpp:124](../src/ui/MainWindow.hpp#L124), [CanvasWidget.hpp:321](../src/ui/CanvasWidget.hpp#L321)|controller가 QObject child UI보다 먼저 파괴되는 raw observer 계약|controller 파괴 후 timer/paint/input 재진입|UAF/segfault|controller-first teardown 후 event pump, ASan|
|R-06|Medium|Confirmed|[ExportWorker.cpp:53](../src/io/ExportWorker.cpp#L53), [RecoveryWriter.cpp:23](../src/app/RecoveryWriter.cpp#L23)|종료가 현재 render/serialization을 무제한 대기|대형 export/autosave 중 close|창 종료 정지|watchdog이 있는 close-during-work test|
|R-07|Medium|High confidence|[CanvasWidget.cpp:546](../src/ui/CanvasWidget.cpp#L546), [CanvasWidgetSelection.cpp:311](../src/ui/CanvasWidgetSelection.cpp#L311)|copy용 <code>m_armSelectionMoveMode</code>가 async generation에 귀속되지 않음|Copy 직후 clear/replace 후 새 selection|다음 selection이 갑자기 move mode 진입|Copy→replace→새 selection async test|
|R-08|Medium|High confidence|[SelectionVisibility.cpp:174](../src/document/SelectionVisibility.cpp#L174)|stale visibility 결과는 거부하지만 작업 자체는 취소되지 않음|대형 animation에서 selection/undo/replace 반복|CPU·RSS 증가, global pool 기아|burst 테스트와 job/RSS 계측|
|R-09|Medium|High confidence|[CanvasWidget.cpp:242](../src/ui/CanvasWidget.cpp#L242), [MainWindowExport.cpp:95](../src/ui/MainWindowExport.cpp#L95), [CanvasWidgetTools.cpp:426](../src/ui/CanvasWidgetTools.cpp#L426)|wobble-off snapshot 처리에서 layer override가 export·pending eyedropper에 남음|layer override + wobble off|화면과 PNG/JPEG 또는 sampled color 불일치|frame>0 pixel 비교|
|R-10|Medium|High confidence|[CanvasWidgetSelection.cpp:1025](../src/ui/CanvasWidgetSelection.cpp#L1025), [CanvasWidget.cpp:1081](../src/ui/CanvasWidget.cpp#L1081)|active lasso rollback snapshot이 resize되지 않고 <code>setLassoMode</code>는 복원 없이 삭제|lasso 중 resize undo 또는 mode 변경|기존 selection 유실|press→Undo/resize→Escape 및 mode-change test|
|R-11|Medium|Hypothesis|[ExportWorker.hpp:84](../src/io/ExportWorker.hpp#L84), [RecoveryWriter.hpp:55](../src/app/RecoveryWriter.hpp#L55)|worker affinity를 가진 value-member QObject를 thread 종료 후 owner thread에서 파괴|반복 worker lifecycle, queued event 잔존|Qt 경고 또는 수명 결함|affinity assertion, 반복 ASan/TSan|
|R-12|Medium|High confidence|[DocumentUndoStack.cpp:257](../src/document/history/DocumentUndoStack.cpp#L257)|history signal 4개를 snapshot 후 순차 emit해 동기 재진입 시 오래된 값이 재발행됨|첫 signal slot이 undo/redo/push|action notification과 실제 cursor 불일치|one-shot reentrant unit test|
|R-13|Low|Confirmed|[HistoryEffects.hpp:20](../src/document/history/HistoryEffects.hpp#L20), [DocumentController.cpp:1359](../src/document/DocumentController.cpp#L1359)|주석은 BeforeEvent 때 이전 document가 current라고 하지만 구현·테스트는 target을 먼저 설치|향후 effect 수정자가 주석을 신뢰|old/new state 뒤집힘|실제 계약 선택 후 테스트로 고정|
|R-14|Low|High confidence|[CanvasWidgetPreview.cpp:434](../src/ui/CanvasWidgetPreview.cpp#L434), [MainWindowActions.cpp:485](../src/ui/MainWindowActions.cpp#L485)|frame shrink가 <code>currentFrameChanged</code>를 누락하고 frame-sensitive selection action이 frame signal을 듣지 않음|frame 수 감소 또는 animated stroke의 frame 변경|stale observer/action 상태|QSignalSpy와 action 상태 비교|
|R-15|Medium|Confirmed|[UguruguTests.cmake:45](../cmake/UguruguTests.cmake#L45), [ci.yml:144](../.github/workflows/ci.yml#L144)|모든 자동 UI 검증이 offscreen이며 TSan 없음|native grab/deactivate/tablet/file-open 또는 data race|CI 통과 후 플랫폼 회귀 잔존|native smoke matrix와 별도 race 검증|

반대로 현재 근거상 잘 방어된 경로도 있습니다.

- frame warming은 immutable snapshot·generation·atomic cancellation을 사용합니다.
- selection 완료는 generation/layer/mask key를 확인합니다.
- LayerDock thumbnail은 snapshot·revision·<code>QPointer</code>를 사용합니다.
- export/recovery worker는 controller의 live reference가 아닌 값 snapshot을 보유합니다.
- <code>RasterAssetCache</code>는 전역 cache 접근을 mutex로 보호합니다.
- 현재 코드에서 구체적인 mutex lock-order deadlock, double delete, stale async callback의 직접 UAF는 발견하지 못했습니다.

## 5. Segfault candidates

확정적으로 재현된 segfault는 없습니다. 아래는 우선 검증할 crash 후보입니다.

### 5.1 Export 메모리 예산 우회 → 예외 경계 이탈

- 관련 위치: <code>MainWindow::exportAnimation</code>, <code>GifExportDialog::updatePresentation</code>, <code>ExportWorker::process/writeAnimation</code>
- 생성·파괴: UI가 <code>Document</code> snapshot을 worker에 전달하고 worker가 각 frame·hierarchy surface·encoder buffer를 할당합니다.
- 무효화 시점: 포인터 invalidation은 확인되지 않았습니다. 주 위험은 과도한 할당과 예외가 Qt event callback을 빠져나가는 것입니다.
- 재현 순서:
  1. 기존 [GifWriterTests.cpp:24](../tests/GifWriterTests.cpp#L24)의 4096×4096, 2-frame deep clipped 문서 사용
  2. UI에서 100% export 선택
  3. size-only 추정 384 MiB로 통과
  4. document-aware 실제 추정 896 MiB가 512 MiB 예산을 초과
- 검증: 메모리 제한 하에서 LLDB의 <code>__cxa_throw</code>, <code>std::terminate</code>, allocation failure breakpoint와 ASan 병행
- 최소 테스트: dialog가 동일 fixture의 100%를 비활성화하거나 worker 시작을 거부하는지 검증
- 예상 crash 형태: 반드시 segfault는 아니며 <code>bad_alloc</code>, terminate, OS kill 가능성이 더 높습니다.

### 5.2 Controller-first 파괴 후 raw observer event

- 관련 위치: <code>MainWindow</code> member 구성, <code>CanvasWidget</code>, <code>TimelineBar</code>, <code>FrameScrubber</code>, <code>WobblePreview</code>
- 생성·파괴:
  1. <code>DocumentController</code>는 <code>MainWindow</code> value member
  2. Canvas와 dock은 QObject child
  3. derived member인 controller가 파괴된 뒤 base <code>QObject</code> destructor가 child를 삭제
- 무효화 시점: controller member destructor 완료 시 raw observer가 dangling이 됩니다.
- 재현 순서: controller와 Canvas를 별도 생성 → controller 먼저 delete → Canvas timer/repaint/mouse/scrub event 처리
- 현재 방어: 정상 <code>MainWindow</code> destructor는 그 사이 이벤트 루프를 돌리지 않고 <code>CanvasWidget</code> destructor도 controller를 읽지 않습니다.
- 검증: ASan, <code>QT_FATAL_WARNINGS=1</code>, timer·paint 강제 처리
- 최소 테스트: owner-first 파괴 계약 테스트. production 정상 종료 재현과 API misuse 테스트를 분리해야 합니다.

### 5.3 Worker/Future 예외 후 terminate 또는 영구 busy

- 관련 위치: [ExportWorker.cpp:92](../src/io/ExportWorker.cpp#L92), [RecoveryWriter.cpp:89](../src/app/RecoveryWriter.cpp#L89), [CanvasWidgetPreview.cpp:564](../src/ui/CanvasWidgetPreview.cpp#L564), [LayerDock.cpp:939](../src/ui/LayerDock.cpp#L939)
- 생성·파괴: queued lambda가 owner <code>this</code>를 사용하고 destructor는 busy가 해제될 때까지 기다립니다.
- 무효화 시점: 정상 경로에는 dangling pointer가 없습니다. 예외 시 <code>complete</code> 또는 <code>m_busy=false</code>가 실행되지 않는 것이 문제입니다.
- 재현 순서: renderer/serializer/future에 예외 주입 → worker event 실행 → 오류 signal과 busy 해제 여부 확인 → owner 파괴
- 검증: 예외 breakpoint, terminate handler, watchdog
- 최소 테스트: 예외 뒤 <code>finished(false)</code>, <code>isBusy()==false</code>, bounded destructor를 모두 검사

### 5.4 Worker-context off-affinity 파괴

- 관련 위치: <code>ExportWorker::m_workerContext</code>, <code>RecoveryWriter::m_workerContext</code>
- 생성·파괴: owner thread에서 value QObject 생성 → worker thread로 이동 → thread quit/wait → owner thread에서 member destructor 실행
- 무효화 시점: 빈 context이고 worker가 이미 정지해 있어 현재의 구체적인 동시 UAF는 확인되지 않았습니다.
- 재현 순서: construct/start/cancel/destroy를 반복하고 queued request가 남은 상태도 포함
- 검증: <code>thread()==QThread::currentThread()</code> 계측, Qt fatal warning, ASan/TSan
- 최소 테스트: idle·active·pending 각각 수천 회 lifecycle 반복
- 판정: Qt 권장 lifecycle과 다른 것은 확정이지만 실제 crash는 가설입니다.

### 5.5 Selection visibility 작업 증폭에 따른 메모리 압박

- 관련 위치: <code>CanvasWidget::evaluateSelectionVisibility</code>
- 생성·파괴: 각 요청이 전체 <code>Document</code>와 mask snapshot을 잡아 global thread pool에 새 작업을 등록합니다.
- 무효화 시점: stale result는 generation으로 차단되므로 직접 UAF 근거는 없습니다.
- 재현 순서: 대형 animated document에서 selection/undo/replacement를 빠르게 반복
- 검증: RSS, queued/running job 수, allocation failure/terminate 관찰
- 최소 테스트: 20회 이상 burst 후 최신 결과만 적용되고 자원 사용이 상한 안에 드는지 확인

문서 교체/Undo 중 live stroke 문제는 현재 layer/mask 검사가 많은 잘못된 commit을 거부하므로 직접 segfault 후보가 아니라 상태·history 회귀 후보입니다.

## 6. Regression hotspots

- selection mask 변경
  → all-frame async visibility
  → selected stroke/action 상태
  → floating source snapshot
  → <code>PixelSelectionOp</code> preview
  → controller history effect
  → frame/cache invalidation

- document replacement
  → prepared state/history/revision reset
  → <code>documentReplaced</code>
  → <code>documentChanged</code>
  → Canvas selection/cache reset
  → LayerDock/Timeline rebuild
  → MainWindow path/recovery metadata

  현재 live stroke와 active lasso가 이 경계에서 빠집니다.

- canvas input press
  → <code>m_activeStroke</code>/lasso transient state
  → Undo/Redo 또는 FileOpen
  → release에서 새 controller state에 commit 시도

- canvas resize
  → 모든 paint layer에 ordered <code>Reframe</code> 추가
  → operation epoch 변경
  → <code>HistoryEffects::CanvasResize</code>
  → selection mask와 lasso geometry 변환
  → preview/thumbnail reset

  현재 rollback snapshot 변환이 누락됩니다.

- export cancellation
  → worker cancel flag
  → 현재 frame render 완료
  → encoder 단계
  → queued completion
  → progress dialog <code>deleteLater</code>
  → 창 close와 destructor wait

- autosave edit
  → edit generation 증가
  → active/pending snapshot
  → recovery revision 완료
  → <code>m_autosavePending</code> 해제

  완료 signal이 request edit generation을 갖지 않는 것이 핵심 결합 결함입니다.

- wobble toggle
  → <code>displayDocument</code>의 document·layer 정규화
  → preview/split/warmup
  → eyedropper
  → static export·thumbnail

  각 경로가 normalization을 복제해 layer override 누락 위험이 있습니다.

- animation frame count
  → current frame normalization
  → frame cache/warmup
  → Timeline/FrameScrubber
  → frame-sensitive editable selection/action 상태

- active layer/hierarchy 변경
  → history의 <code>UsePrepared</code>/<code>PreserveCurrentIfPresent</code> 정책
  → selection clear
  → dock sync
  → thumbnail effect
  → clipboard paste/group/merge 동작

## 7. Test gap analysis

현행 테스트 구조는 하나의 <code>ugurugu_tests</code> 실행 파일을 환경변수로 9개 suite로 분할합니다. [TestMain.cpp:23](../tests/TestMain.cpp#L23)는 임시 QSettings와 recovery 경로를 사용하며, UI 테스트도 실제 <code>QApplication</code>과 widget을 생성합니다. 다만 event backend는 전부 offscreen입니다.

현재 비교적 충분한 영역:

- History clean/merge/macro/preflight/backing/memory/atomic callback: [DocumentHistoryTests.cpp:13](../tests/DocumentHistoryTests.cpp#L13)
- 문서 교체 준비 실패와 nested replacement 원자성: [DocumentLifecycleTests.cpp:363](../tests/DocumentLifecycleTests.cpp#L363)
- layer hierarchy·merge·override·paste 제한: [LayerCommandTests.cpp:12](../tests/LayerCommandTests.cpp#L12)
- schema·직렬화 예산·asset deduplication
- render hierarchy·preview cache·selection operation·GIF/WebP 결과
- 정상 selection undo/copy/cut/floating/save/autosave/resize 경로
- focus/deactivate 때 mouse/tablet/lasso/move 취소

부족한 영역:

|우선순위|검증 불변식|입력·이벤트 순서|탐지 버그|종류|Sanitizer|
|---|---|---|---|---|---|
|P0|애니메이션 export는 hierarchy 포함 예산을 지켜야 함|deep fixture→dialog→100% 선택|OOM 정책 우회|UI integration|보조적으로 ASan|
|P0|최신 edit 성공 전 autosave pending 해제 금지|A active→B pending→A 성공→B 실패|stale recovery, retry 누락|integration + failure seam|불필요|
|P0|입력 transaction은 문서 경계를 넘지 않음|stroke/lasso press→FileOpen/new→release|새 문서 오염, 잘못된 selection|UI integration|ASan/UBSan|
|P0|history movement 전 live input 대칭 종료|press→Undo/Redo→release/Escape|redo tail 손실, old geometry commit|UI integration|ASan/UBSan|
|P0|controller observer 수명 안전|controller 먼저 파괴→timer/paint/input|UAF|lifecycle integration|필수 ASan|
|P1|종료가 bounded이고 partial file 없음|export/recovery 시작 즉시 close|hang, teardown race|integration + watchdog|ASan|
|P1|async 실패도 busy 정리|render/serializer 예외 주입|terminate, 영구 wait|unit/integration|예외 도구가 핵심|
|P1|copy arm은 해당 generation에만 귀속|Copy→즉시 clear/replace→새 selection|예상 밖 move mode|async UI|선택|
|P1|stale selection 작업은 자원 상한 유지|selection/undo/replace burst|thread-pool 기아, OOM|performance integration|ASan+RSS|
|P1|화면·export·picker의 wobble 의미 동일|layer override→wobble off→frame>0|픽셀/색 불일치|render/UI integration|불필요|
|P1|lasso cancel은 이전 selection 복구|active lasso→resize undo 또는 mode 변경|selection 유실|UI|불필요|
|P2|history signal은 재진입 후 실제 cursor와 일치|<code>canUndoChanged</code> slot에서 undo|stale action signal|core unit|불필요|
|P2|frame observer/action 상태 최신성|frame 수 축소, animated partial selection frame 변경|stale signal/action|UI unit|불필요|
|P1|native event 순서 검증|실제 tablet, mouse grab, deactivate, FileOpen, modal close|offscreen 전용 통과 후 플랫폼 회귀|native GUI smoke|ASan 병행|

Sanitizer 사각지대:

- ASan/UBSan은 data race를 검출하지 않습니다.
- offscreen 테스트가 실행하지 않는 Cocoa/Windows native event·tablet·file-open 순서는 instrument되지 않습니다.
- OOM 정책 오류, 종료 hang, signal 재진입, autosave generation 오류는 sanitizer 대상이 아닙니다.
- Windows updater는 이번 macOS 빌드에서 실행되지 않았습니다.
- worker affinity 위반은 실제 동시 접근이 없으면 ASan이 보고하지 않을 수 있습니다.

Flaky 가능성:

- 이번 실행에서는 flaky failure가 없었습니다.
- 다수 UI 테스트가 <code>QTRY_*</code>, 1–10초 timeout, global thread pool 완료에 의존합니다.
- 일부 성능 테스트는 5초 wall-clock threshold를 사용합니다.
- Release에서 [LayerSplitPreviewTests.cpp:522](../tests/LayerSplitPreviewTests.cpp#L522)가 workload를 1천/5천 point에서 1만/5만 point로 확대하므로 Debug와 Release의 실행 범위가 동일하지 않습니다.
- [UiViewportTests.cpp:429](../tests/UiViewportTests.cpp#L429)의 pen-up latency assertion도 Release에서만 활성화됩니다.
- 반대로 <code>Q_ASSERT</code> 기반 전제는 Release에서 사라집니다. 예: [DocumentController.cpp:382](../src/document/DocumentController.cpp#L382), [DocumentController.cpp:1324](../src/document/DocumentController.cpp#L1324).

## 8. Recommended debugging workflow

1. 재현 절차 확립

   실제 입력 장치, 문서 상태, layer ID, history index, frame, pending transform, worker 상태까지 기록합니다. native와 offscreen 재현을 구분합니다.

2. 실패하는 회귀 테스트 작성

   가장 낮은 계층의 deterministic test를 먼저 만들고, lifecycle 문제는 별도의 UI integration test로 실제 이벤트 순서를 고정합니다.

3. 관련 상태 불변식 문서화

   수정 전 해당 경로의 source document identity, history node/revision, selection mask size/layer, worker request generation을 테스트 이름과 assertion으로 명시합니다.

4. 최소 패치

   재현된 불변식 하나만 복구합니다. history·serializer·render cache를 함께 정리하는 식의 확대 수정은 피합니다.

5. 관련 테스트 실행

   관련 suite와 새 단일 테스트를 Debug에서 반복 실행합니다. async 테스트는 단순 sleep보다 barrier/failure seam을 사용합니다.

6. 전체 테스트 실행

   ~~~sh
   cmake --preset macos-debug
   cmake --build --preset macos-debug
   ctest --preset macos-debug

   cmake --preset macos-release
   cmake --build --preset macos-release
   ctest --preset macos-release
   ~~~

7. Sanitizer 실행

   ~~~sh
   cmake --preset macos-sanitized
   cmake --build --preset macos-sanitized
   ctest --preset macos-sanitized
   ~~~

   race 가능성이 있으면 별도의 TSan 또는 thread-affinity 계측도 필요합니다.

8. Diff 기반 회귀 검토

   ownership, lambda capture, receiver context, signal 순서, history direction, cache invalidation, cancel/error/close 비대칭을 변경 줄뿐 아니라 호출자까지 역추적합니다.

9. 실제 GUI 수동 검증

   mouse와 tablet 각각으로 press 중 Undo/Open/Deactivate, selection transform 중 Save/Export/New, export/autosave 중 Close, animation frame 변경, native file-open event를 확인합니다. Windows updater 변경이라면 Windows에서도 별도 실행해야 합니다.

어떤 단계에서도 전체 테스트 통과만으로 수명 안전성을 결론 내리지 않아야 합니다.

## 9. Prioritized action plan

즉시 조사:

1. R-01 deep hierarchy animation export UI preflight
2. R-02 autosave A-success/B-failure 완료 신원
3. R-03 문서 교체 및 Undo/Redo 중 live stroke/lasso
4. R-05 controller-first observer 파괴
5. R-04 worker 예외와 R-06 종료 liveness

테스트를 먼저 추가:

- request별 autosave generation/session/revision 테스트 seam
- live input과 document/history boundary 조합
- copy async arm generation
- layer wobble override의 export/picker 비교
- active lasso resize/mode-change rollback
- close-during-export/recovery watchdog
- history signal 재진입

Instrumentation이 필요한 항목:

- Canvas interaction 시작 시 history node/content revision/document identity/layer ID
- replacement와 Undo/Redo 경계에서 active input 상태
- autosave request의 session/edit generation/revision
- async selection의 generation, queue/running job 수, snapshot bytes
- worker start/cancel/finish/exception과 종료 대기 시간
- controller observer의 thread·destroyed 상태

재현 후 검토할 구조적 개선:

- 모든 document/history boundary가 하나의 Canvas interaction 종료 경로를 통과하도록 할 것
- <code>m_armSelectionMoveMode</code>를 generation-scoped token으로 만들 것
- autosave 완료 signal에 요청 identity를 포함할 것
- wobble-off display/export/picker용 snapshot 정규화를 공통 정책으로 둘 것
- worker 예외 정리와 cancellation을 RAII 상태 전이로 보장할 것
- worker context를 affinity thread에서 파괴하는 lifecycle을 명시할 것

현재 건드리지 않는 것이 나은 영역:

- <code>PreparedDocument</code> freezer와 immutable backing lease
- <code>DocumentDelta</code>와 history memory accounting
- ordered <code>PixelSelection</code>/<code>Reframe</code> operation epoch
- layer split/raster preview 최적화
- serializer asset deduplication

이 영역들은 영향 범위가 매우 넓고 현재 검증도 상대적으로 강합니다. 명시적인 실패 재현 없이 “정리” 목적으로 변경하면 세그멘테이션 폴트보다 더 발견하기 어려운 저장·Undo·렌더 회귀를 만들 가능성이 큽니다.

가장 먼저 고칠 코드를 고르는 단계가 아니라, 가장 먼저 검증해야 할 위험은 **애니메이션 export 예산 우회, autosave 완료 신원, live input의 문서/history 경계 누수**입니다.
