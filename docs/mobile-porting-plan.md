# Ugurugu 모바일 이식 개발 계획

> - 아키텍처·코드 조사 기준: 저장소 커밋 `dbd497c7003579b6b5d3253d448f3fd1785dff30` (`Prepare the 2.2.1 release`)
> - 라이선스·기여자 권리 보충 기준: 저장소 커밋 `09c75821a437f9c75fcb2e7420d18d5093ba9eda` (`Remove outdated audit report for 2026-08-06`)
> - 공식 문서 확인일: 2026-08-06 (KST)
> - 대상: iPhone, iPad, Android 스마트폰, Android 태블릿
> - 조사 방식: 소스·빌드·테스트·패키징 설정의 정적 감사와 공식 문서 검토. 요청에 따라 빌드, 리팩터링, 설정 변경, PoC 및 기기 측정은 수행하지 않았다.

## 의사결정 요약

모바일 이식은 가능하다. 다만 현재 Qt Widgets 데스크톱 UI를 축소해 올리는 포팅은 제품 수준의 해법이 아니다. 가장 현실적인 방향은 **Qt for iOS/Android 위에서 기존 Qt/C++ 코어를 유지하고, iOS·Android가 함께 쓰는 새 Qt Quick/QML 모바일 UI를 만들며, 파일·수명주기·고충실도 펜 입력만 얇은 Objective-C++/UIKit 및 Kotlin/Java 어댑터로 연결하는 것**이다. 즉 구현 방향 1을 기반으로 하되, 구현 방향 5에서는 **데스크톱 Widgets UI와 모바일 QML UI를 분리**한다.

예산 0원의 학생 프로젝트라는 제약에서는 **Android 태블릿을 첫 MVP 플랫폼으로 추천한다**. Qt Community Edition과 GPL-3.0-or-later 소스 공개를 유지한다. 첫 검증은 최대 20대의 닫힌 Limited Distribution alpha로 하고, 그다음 developer-signed APK와 대응 source를 public release page에 무료 공개할 수 있다. 다만 20대 밖의 사용자는 Android Developer Verification 확대에 따라 advanced flow 또는 ADB를 거칠 수 있다. 저마찰 광범위 Full Distribution은 일회성 US$25이므로 “직접 APK는 영구적으로 무제한·무마찰 무료”라고 가정하지 않는다. iPad는 Apple Pencil·Metal·UIKit 입력을 검증하는 본인 기기용 기술 PoC로 병행하되, 타인에게 제공하는 TestFlight와 App Store 출시는 배포 권리가 서면으로 확정될 때까지 보류한다.

가장 먼저 통과해야 할 P0 게이트는 기술이 아니라 배포 권리다. Ugurugu는 GPL-3.0-or-later이고(`README.md@09c7582:196-210`), 현재 Qt 고지는 교체 가능한 동적 링크 배포를 전제로 한다(`THIRD_PARTY_NOTICES.md@09c7582:12-23`). 앱 가격이 무료여도 GPL/LGPL 의무는 줄지 않는다. 공개 release에는 학습용으로 제한된 Qt Educational License 바이너리를 사용하지 않고 **Qt Community Edition**만 사용한다. Android 공개판은 complete corresponding source, 정확한 build·재서명·설치 방법, Qt 소스 제공 방법과 license notice를 함께 준비한다. iOS 정적 링크와 App Store 조건은 **앱 저작권 보유 현황·재라이선스 가능성·Qt LGPL 준수 또는 별도 서면 허가**를 전문 법률 검토로 확정해야 한다. Qt 공식 FAQ상 Community/open-source Qt로 시작한 프로젝트의 상용 전환은 Qt Company의 사전 서면 동의가 필요하다. Qt 상용 라이선스만으로 Ugurugu 자체 GPL 문제가 자동 해결되는 것도 아니다. 이 문서는 법률 자문이 아니다.

작업 규모의 기준 추정치는 모바일 경험이 있는 C++/Qt 엔지니어 2명, 플랫폼 엔지니어 1명, 파트타임 디자인·QA를 가정할 때 다음과 같다. 예산 0원의 1인 학생 개발에서는 같은 인월이 순차 작업으로 바뀌므로 아래 달력 기간을 그대로 적용하지 말고 Android 태블릿만 먼저 닫아야 한다.

- Android 태블릿 배포 MVP: 약 **10~16 인월**, 기준 팀 병렬화 시 **4~6개월**
- 네 가지 폼팩터의 스토어 출시 품질: 약 **21~33 인월**, **9~13개월**
- 1인 파트타임 개발: 전체 네 폼팩터는 다년 프로젝트가 되므로 Android 태블릿 → Android 스마트폰 → iPad/iPhone 순으로 출시 범위를 나눈다.
- 전제: 계정·클라우드 동기화·협업 기능·완전한 GPU 브러시 엔진은 범위 밖이며, 라이선스 협상 기간은 제외한다.
- 신뢰도: 중간 이하. 특히 직렬화 피크 메모리, 실제 펜 이벤트 품질, Android GPU 드라이버 편차는 PoC 전에는 ±30% 이상의 일정 변동을 만들 수 있다.

## 조사 기준과 판단 원칙

- 코드 위치는 `경로:시작줄-끝줄` 형식으로 표시한다.
- 재사용률은 물리 LOC가 아니라 **의미 있는 기존 구현이 큰 재작성 없이 제품 코드로 남는 비율**의 계획 추정치다.
- 성능·앱 크기·시작 시간은 정적 분석만으로 숫자를 확정하지 않는다. 표의 평가는 상대 비교이며, 실제 수치는 1단계 release/thinned 빌드에서 측정한다.
- Apple Developer Program 가입 비용과 가입 절차는 제외한다. 인증서, 프로파일, App ID, 서명, TestFlight, App Store Connect 및 심사는 포함한다.
- 이 product tree의 Android와 iOS PoC·release는 모두 Qt Community Edition으로 통일한다. Qt Educational License는 별도의 학습 sandbox에서만 사용하고 Ugurugu source·build·artifact와 섞지 않는다.
- 현재 공식 지원선은 Qt 6.11 문서 기준 Android 9/API 28~Android 16/API 36, iOS 17 이상이다. 스토어 제출 시점에는 다시 확인해야 한다.

## 1. 현재 코드베이스의 모바일 이식 준비 상태

### 1.1 전체 구조

현재 애플리케이션은 Qt 6/C++23 기반의 데스크톱 앱이며, `QApplication`과 `QMainWindow`가 수명주기와 화면 구성을 소유한다(`src/main.cpp:150-229`, `src/ui/MainWindow.hpp:36-130`). 핵심 경계는 이미 두 정적 라이브러리로 나뉜다.

```text
Ugurugu 실행 파일 / QApplication
  └─ ugurugu_ui
       ├─ MainWindow, dock, dialog, action, settings
       ├─ CanvasWidget: 입력 + 세션 + 캐시 + 스케줄링
       └─ CanvasFrameView: QRhi 표시
            └─ ugurugu_core
                 ├─ document / controller / history
                 ├─ brush / input stabilizer
                 ├─ serializer / import / export / recovery
                 └─ CPU QImage/QPainter renderer
```

`ugurugu_core`는 의도적으로 Qt Widgets를 링크하지 않고 Qt Core·Gui, spdlog, libwebp만 사용한다(`cmake/UguruguTargets.cmake:9-24`). UI는 Qt Widgets·Concurrent와 private QRhi 헤더를 위해 `Qt6::GuiPrivate`를 링크한다(`cmake/UguruguTargets.cmake:26-46`). 이 경계는 모바일 공통 코어의 좋은 출발점이다.

정적 물리 줄 수는 대략 다음과 같다. 이는 재사용률 자체가 아니라 상대 규모를 보여준다.

| 영역 | 대략적 줄 수 | 모바일 판단 |
|---|---:|---|
| `src/document` | 9.3K | 높은 재사용 후보 |
| `src/io` | 8.8K | codec은 공유, 저장 매체는 추상화 필요 |
| `src/render` | 8.4K | CPU 기준 렌더러로 높은 재사용 후보 |
| `src/app`, `src/input`, `src/brush` | 2.4K | 정책과 플랫폼 코드를 분리해야 함 |
| `src/ui` | 22.2K | 대부분 모바일용 재설계 |
| 테스트 | 25.7K | 코어 회귀 자산은 강함, 모바일 검증은 없음 |

### 1.2 UI와 입력

- 창 최소 크기는 900×640이고 중앙 캔버스·타임라인과 여러 `QDockWidget`을 조합한다(`src/ui/MainWindow.cpp:160-187`, `227-252`). 도킹, 메뉴, 상태바, 전역 팝업, 고정 크기 대화상자는 데스크톱 전제다.
- `CanvasWidget` 한 클래스가 문서 컨트롤러, 도구 상태, 선택, 프레임 재생, QImage 캐시, thread pool, 활성 stroke, 입력 상태, QRhi 표시를 함께 소유한다(`src/ui/CanvasWidget.hpp:228-283`, `368-507`). 모바일 view가 재생성되거나 scene/activity가 전환되는 구조에 맞게 세션과 view를 분리해야 한다.
- 터치 이벤트는 명시적으로 꺼져 있다(`src/ui/CanvasWidget.cpp:52-57`). 모바일 멀티터치 경로가 없다.
- 태블릿 이벤트는 위치, 압력, 지우개, timestamp만 사용한다(`src/ui/CanvasWidgetEvents.cpp:433-595`). `QTabletEvent`가 제공하는 x/y tilt, rotation, tangential pressure를 버리고, coalesced/historical/predicted sample과 취소 rollback도 없다.
- `StrokePoint`가 저장하는 것은 좌표와 압력뿐이다(`src/document/Document.hpp:164-170`). tilt·azimuth가 brush 결과에 영향을 주게 하려면 문서 schema, 보간, renderer, 구버전 기본값까지 함께 바뀌어야 한다.
- 현재 viewport는 translate/scale/horizontal mirror 중심이며 회전 제스처 상태가 없다(`src/ui/CanvasWidgetPreview.cpp:21-36`). NativeGesture zoom, wheel zoom, 마우스/Space pan은 있지만 두 손가락 pan/zoom/rotation은 없다(`src/ui/CanvasWidgetEvents.cpp:47-72`, `210-430`).

### 1.3 렌더링

현재 파이프라인은 **GPU 브러시 엔진이 아니라 CPU 래스터 엔진 + GPU 최종 표시**다.

```text
Stroke / Document
  → IncrementalStrokeRenderer·RenderEngine
  → CPU QImage/QPainter 합성
  → DisplayedFrame(QImage + dirty bounds)
  → BGRA8 texture 부분 업로드
  → QRhi textured quad
  → 화면
```

- `RenderEngine::render()`와 `renderScaled()`는 QImage를 만들고 stroke/fill/selection/layer를 CPU로 재생한다(`src/render/RenderEngine.hpp:16-25`, `135-140`; `src/render/RenderEngine.cpp:38-103`).
- 활성 stroke는 256×256 타일과 checkpoint를 사용하고 변경 영역만 다시 만든다(`src/render/IncrementalStrokeRenderer.hpp:34-58`, `src/render/IncrementalStrokeRenderer.cpp:48-203`). 이것은 모바일에서도 가치가 크다.
- `CanvasFrameView`는 CPU 합성 QImage를 QRhi BGRA8 texture로 업로드하고 한 개의 textured quad로 표시한다(`src/ui/CanvasFrameView.hpp:14-47`, `src/ui/CanvasFrameView.cpp:77-204`, `208-261`). 따라서 Metal/Vulkan/OpenGL ES 선택은 현재 문서 렌더 알고리즘보다 표시·업로드·합성 성능에 영향을 준다.
- dirty bounds 부분 업로드와 display-scale replay, layer split/raster cache, surface pool은 좋은 자산이다(`src/ui/CanvasWidgetPreview.cpp:147-308`, `352-490`; `src/render/LayerCompositionPlan.hpp:11-48`; `src/render/engine/LayerHierarchyCompositor.hpp:17-49`, `157-271`).
- 반면 texture 한계, GPU 메모리 계상, allocation/device-loss 실패 정책과 실제 모바일 backend 테스트가 없다. QRhi private API는 Qt minor 버전 간 source/binary 호환 보장이 없으므로 정확한 Qt patch pinning이 필요하다.

MVP에서는 렌더러를 Metal/Vulkan으로 전면 재작성하지 않는다. CPU renderer를 파일 호환성과 pixel golden의 기준 구현으로 유지하고, QQuickRhiItem을 표시 경로의 1차 후보로 검증한다. 타일 기반 장기 개선이나 GPU brush preview는 실제 병목이 입증된 뒤 별도 단계로 둔다.

### 1.4 문서 모델, 직렬화, 파일 I/O

- `Document`는 canvas, animation, raster asset, 계층형 layer와 ordered stroke/operation을 가진다(`src/document/Document.hpp:172-195`, `215-250`). 명령 재생 모델이라 비파괴 편집과 파일 호환에는 유리하다.
- 자체 undo stack은 Qt Widgets의 `QUndoCommand`에 의존하지 않고 structural delta와 implicit sharing을 사용한다(`src/document/history/LogicalHistoryCommand.hpp:15-26`, `src/document/history/DocumentDelta.hpp:16-129`). 모바일 공통 코어에 적합하다.
- 현재 schema 13, rendering algorithm 3이며 load/save 양쪽에 크기·계층·mask/raster 검증이 있다(`src/io/serializer/SerializerSchema.hpp:16-25`, `src/io/serializer/DocumentValidation.cpp:26-453`). 저장은 `QSaveFile` atomic replace다(`src/io/DocumentSerializer.cpp:599-616`).
- 그러나 public I/O는 `QString filePath`에 고정되어 있다(`src/io/DocumentSerializer.cpp:562-628`). iOS security-scoped URL과 Android `content://` URI의 identity·권한·provider capability를 표현하지 못한다.
- 최대 128 MiB 파일을 통째로 `QByteArray`에 읽고 QJson DOM을 만든다(`src/io/DocumentSerializer.cpp:631-648`, `701-724`). 저장도 전체 JSON bytes를 먼저 만든다(`src/io/DocumentSerializer.cpp:591-611`). mobile peak RSS의 핵심 위험이다.
- animation export는 모든 프레임을 `QVector<QImage>`에 보관한 뒤 encoder로 보낸다(`src/io/ExportWorker.cpp:296-372`). 4K RGBA 한 장은 64 MiB이므로 데스크톱 예산에서는 통과해도 mobile OOM이 날 수 있다.

### 1.5 메모리, 스레딩, 복구

- 최대 canvas는 4096×4096, animation은 최대 60 frame이고 project/mask/raster에도 큰 상한이 있다(`src/document/DocumentLimits.hpp:8-50`). 4K RGBA surface 한 장은 64 MiB다.
- 메모리 정책은 resident 4 GiB, history 192 MiB, decoded raster 128 MiB, serialization/export working set 각각 512 MiB를 전제로 한다(`src/app/MemoryBudget.hpp:8-31`).
- 설치 RAM 탐지는 Windows와 macOS만 구현되어 iOS·Android에서는 0을 반환하고 preview cache 최소 128 MiB를 선택한다(`src/app/MemoryBudget.cpp:27-48`, `59-68`). 저사양 Android에 부적합하다.
- animation warmup은 `idealThreadCount() - 2`를 1~8 worker로 clamp한다(`src/ui/CanvasWidget.cpp:169-175`). 모바일 big.LITTLE 장치에서 여러 full-frame render가 동시에 메모리·열·배터리를 압박할 수 있다.
- background 실행 경로가 하나로 통합되어 있지 않다. `ExportWorker`는 전용 `QThread`를 시작하고 destructor에서 deadline 없이 기다리며(`src/io/ExportWorker.cpp:46-62`), canvas warmup은 별도 `QThreadPool`, layer thumbnail과 selection visibility는 global QtConcurrent pool을 쓴다(`src/ui/CanvasWidgetPreview.cpp:958-970`, `src/ui/LayerDock.cpp:1002-1009`, `src/ui/CanvasWidgetSelection.cpp:393-430`). 앱 종료는 global pool 전체를 기다린다(`src/main.cpp:289`, `src/app/BackgroundWork.cpp:8-11`).
- 30초 autosave, 최신 pending snapshot 대체, 전용 writer thread, generation-guarded `QSaveFile`은 좋은 기반이다(`src/ui/MainWindow.cpp:220-225`, `1286-1352`; `src/app/RecoveryWriter.cpp:40-63`, `178-238`).
- 하지만 writer destructor는 deadline 없이 flush/join하고(`src/app/RecoveryWriter.cpp:24-38`), recovery는 프로세스 전체에 `recovery.ugu` 하나뿐이다(`src/app/RecoveryStore.cpp:186-196`). iOS scene, Android activity/process death, 빠른 suspend에는 맞지 않는다.

모바일에서는 이 실행 경로를 job type, owner document, cancellation deadline, memory reservation과 foreground requirement가 있는 공통 scheduler 뒤로 모아야 한다. view가 사라질 때 watcher callback이 파괴된 UI를 만지지 않게 session generation을 검사하고, suspend/종료에서 global pool 전체를 무기한 join하는 방식에 의존하지 않는다.

### 1.6 빌드, 외부 라이브러리, CI와 배포

- CMake 3.31/C++23, Qt 6.10 이상을 요구하며 배포 preset/CI는 Qt 6.11.1을 고정한다(`CMakeLists.txt:1-13`, `cmake/UguruguDependencies.cmake:3-30`, `CMakePresets.json:75-80`, `123-128`).
- spdlog 1.16.0과 libwebp 1.6.0을 FetchContent로 고정한다(`cmake/UguruguDependencies.cmake:32-82`). Apple 분기는 Sparkle 2.9.4, Windows는 Velopack 1.2.0을 포함한다(`cmake/UguruguDependencies.cmake:89-146`).
- 현재 모든 `APPLE`을 macOS로 간주한다. macOS deployment target, Sparkle, AppKit, `.icns`, macOS Info.plist와 bundle packaging이 iOS에도 선택된다(`CMakeLists.txt:3-10`, `cmake/UguruguDependencies.cmake:89-115`, `cmake/UguruguTargets.cmake:48-90`, `139-157`). 그대로는 iOS configure가 성공할 구조가 아니다.
- source-level 종속 코드도 macOS `NSWindow` chrome과 Sparkle updater(`src/ui/MacWindowChrome.mm:8-35`, `src/app/UpdateControllerMac.mm:5-53`), Windows private native interface/Wintab 초기화(`src/main.cpp:25-28`, `46-55`, `209-220`)로 나뉜다. 이 구현은 desktop app target에 남기고 `ugurugu_core`로 새지 않게 해야 한다.
- preset, CI matrix, packaging, test 실행기는 macOS/Windows만 지원한다. iOS/Android target, manifest, entitlement, asset catalog/adaptive icon, signing, simulator/device CI가 없다(`CMakePresets.json:20-249`, `.github/workflows/ci.yml:139-210`, `cmake/UguruguTests.cmake:1-68`).
- 테스트는 serialization/history/recovery/render golden과 메모리 경계가 강점이지만 모두 desktop/offscreen 중심이다. 실제 Metal/Vulkan/GLES, stylus, lifecycle, security-scoped URL/SAF, memory pressure 테스트는 없다.

### 1.7 준비 상태 판정

| 영역 | 준비도 | 판단 |
|---|---:|---|
| 문서·히스토리·명령 | 4/5 | 잘 테스트된 공통 코어 후보 |
| CPU 렌더·incremental preview | 4/5 | 공유 가능하나 mobile budget·scheduler 필요 |
| 직렬화 codec·검증 | 3/5 | 형식은 강함, full-buffer/DOM 피크가 위험 |
| 파일·문서 identity | 1/5 | direct path 전제 |
| autosave·복구 | 2/5 | 알고리즘은 좋으나 mobile lifecycle 부적합 |
| UI·adaptive layout | 1/5 | desktop dock 중심, 신규 설계 필요 |
| 펜·멀티터치 | 1/5 | pressure만 있음, touch는 꺼짐 |
| 모바일 빌드·CI·스토어 | 0/5 | target과 pipeline 없음 |
| 전체 | **코어 3.5/5, 출시 준비 1/5** | “가능하지만 직접 포팅은 아님” |

## 2. 그대로 공유 가능한 모듈

“그대로”는 Qt 6.11 계열을 iOS/Android에도 사용하고 mobile 정책을 외부에서 주입한다는 뜻이다. 아래 구현의 알고리즘과 대부분의 테스트는 유지할 수 있다.

| 모듈 | 공유할 내용 | 필요한 제한 조건 |
|---|---|---|
| `src/document/Document.*`, `LayerHierarchy.*`, `DocumentOperations.*` | 문서 구조, layer hierarchy, mask/selection/fill 연산 | tilt schema를 추가하면 version/default 규칙 필요 |
| `src/document/history/*` | delta 기반 undo/redo, macro, preflight | 기기별 byte/count limit과 memory-pressure 축소 정책 주입 |
| `DocumentController`의 command 계층 | 편집 명령, revision, signal | UI thread/owner 계약을 façade로 고정 |
| `src/io/serializer/*` | schema, validation, canonical raster, JSON codec | QIODevice/streaming 경계와 peak 감소 필요 |
| `WawaV10Reader`, `WawaV10Importer` | legacy bounded parser와 변환 | 모바일 MVP에서는 UI 노출을 연기 가능 |
| brush model, `StrokeStabilizer` | 압력 dynamics와 안정화 | 표준화된 pointer sample 입력으로 교체 |
| `RenderEngine`, `StrokeRenderer` | pixel 기준 CPU renderer | mobile cache/scheduler와 교차 플랫폼 golden 필요 |
| `IncrementalStrokeRenderer` | 256px tile active-stroke preview | cancellation과 메모리 reservation 강화 |
| layer composition/display-scale replay | 부분 replay, split/raster cache, surface plan | 복잡 문서 fallback peak를 측정 |
| GIF/WebP encoder의 형식 로직 | 출력 codec | 모든 frame 동시 보관을 streaming으로 변경 |
| recovery metadata/schema와 generation 규칙 | 복구 정확성, atomicity | storage repository와 scene/document identity 분리 |
| 기존 core 테스트 | serializer/history/render/recovery 회귀 | mobile core-only executable과 ARM CI로 분리 |

Qt 타입(`QImage`, `QPainter`, `QString`, `QVector`, `QObject`)이 공개 API에 넓게 퍼져 있으므로 이 평가는 Qt 기반 모바일에만 성립한다. SwiftUI/Compose만 쓰는 방향에서는 동일 코드를 호출하더라도 DTO·C ABI·Objective-C++/JNI façade를 새로 만들어야 하므로 실질 재사용률이 낮아진다.

## 3. 플랫폼 추상화가 필요한 모듈

### 3.1 필요한 경계

| 추상화 | 공통 계약 | iOS 구현 | Android 구현 |
|---|---|---|---|
| `DocumentHandle` | stable identity, display name, readable/writable stream, replace/copy capability, persisted access token | security-scoped URL/bookmark, `NSFileCoordinator`/`NSFilePresenter`, iCloud/File Provider | `content://` URI, `ContentResolver`, persistable grant, provider flags |
| `DocumentRepository` | open/save/save-as, internal temp 후 commit, conflict/error | UIDocumentPicker/UIDocumentViewController | SAF `ACTION_OPEN_DOCUMENT`/`ACTION_CREATE_DOCUMENT` |
| `LifecycleAdapter` | foreground/background, suspend deadline, scene/session ID | UIScene events, background task expiration, memory warning | QtActivity callbacks, process-death state, memory/trim callbacks |
| `RecoveryRepository` | per-document/per-scene latest snapshot, journal, quarantine | Application Support | internal `filesDir` |
| `ShareService` | typed export item과 temporary lifetime | `UIActivityViewController` | Android Sharesheet `ACTION_SEND`, `FileProvider` URI |
| `ImageImportService` | stream + metadata + permission lifetime | Photos/document picker | Photo Picker/SAF |
| `MemoryBudgetProvider` | device tier, foreground/background budget, pressure event | physical footprint/memory warning에 맞춘 정책 | RAM/memory class, LMK history, current window, trim event |
| `WorkScheduler` | concurrency, cancellation, memory reservation, thermal/background policy | QoS와 scene 상태 | core count·thermal·battery·activity 상태 |
| `PointerInputAdapter` | normalized sample batch, cancel semantics | UIKit coalesced/predicted/estimated Pencil touches | MotionEvent history/prediction/cancel/tool axes |
| `WindowEnvironment` | logical size, DPR, safe insets, keyboard/pointer, hinge regions | trait/scene/safe area | Window metrics, insets, WindowManager fold info |
| `FramePresenter` | QImage/dirty rect/view transform, backend failure | QRhi/Metal | QRhi Vulkan/GLES |
| settings/logging/clipboard/open-request | 값·event 계약 | UserDefaults/OS pasteboard/open URL | preferences/ClipboardManager/intent |

### 3.2 문서 I/O의 핵심 원칙

외부 문서를 absolute path로 축약하지 않는다. 공통 코어는 path 대신 `QIODevice` 또는 명시적인 read/write session을 받고, 플랫폼 계층이 권한과 identity를 소유한다.

Qt 6.11은 iOS에서 native save dialog와 사용자가 선택한 외부 security-scoped resource의 읽기·쓰기 및 재실행 후 접근 지원을 추가했다. 먼저 이 표준 경로로 picker/open/save를 검증하고, stable document identity, provider conflict, coordinated commit 또는 bookmark 복구가 부족할 때만 Objective-C++ 구현을 보강한다. 공통 `DocumentRepository` 계약은 어느 구현을 선택해도 유지한다.

1. 편집 중 recovery는 항상 app sandbox 내부에 빠르고 atomic하게 저장한다.
2. 외부 provider 원본 저장은 별도의 coordinated commit으로 처리한다.
3. provider가 atomic rename이나 seek를 보장하지 않으면 internal temporary file을 완성한 뒤 copy/replace한다.
4. 권한 취소, stale bookmark, read-only provider, cloud conflict, 일시적 네트워크 실패를 정상 상태로 모델링한다.
5. iOS에는 bookmark/security scope를, Android에는 URI와 persistable grant flags를 저장한다. 문자열 path만 저장하지 않는다.

내부 저장소도 용도를 구분한다. iOS의 durable recovery와 metadata는 Application Support, 재생성 가능한 preview는 Caches, commit 중간 산출물은 temporary directory에 둔다. Android에서는 같은 역할을 `filesDir`, `cacheDir`, cache/temp file로 나눈다. 최종 사용자 문서를 이 위치에만 두면 앱 삭제 시 함께 사라질 수 있으므로 iOS Files/iCloud 또는 Android SAF로 명시적으로 내보낸다. Qt `QStandardPaths`가 반환한 path를 사용할 때도 그 의미와 backup 정책은 platform adapter가 정한다.

### 3.3 background 자동 저장과 복구

수명주기 callback에서 처음으로 전체 문서를 serialize하지 않는다. edit command마다 작은 journal 또는 copy-on-write snapshot 후보를 만들고 pen-up/idle 때 checkpoint를 준비한다.

- iOS는 scene이 active를 떠날 때 입력·playback·warmup을 멈추고 이미 준비된 journal을 flush한다. background task를 쓰더라도 expiration handler와 짧은 deadline을 가지며 성공을 전제로 하지 않는다. memory warning에는 재생성 가능한 cache부터 버린다.
- Android `onPause()`는 매우 짧으므로 disk/database 작업을 시작하지 않는다. `onStop()`보다 앞서 recovery point를 준비하고, Qt가 stop 뒤 thread를 suspend할 수 있음을 전제로 짧은 native/internal flush만 남긴다. instance state bundle에는 document bitmap/JSON이 아니라 document ID, URI token, revision, viewport 같은 작은 복구 key만 넣는다.
- 한 개의 `recovery.ugu` 대신 document/scene별 generation과 committed revision을 index한다. 시작 시 원본보다 새 recovery만 제안하고, corrupt snapshot은 quarantine하며 원본을 덮지 않는다.
- 외부 provider save와 internal recovery를 분리한다. background deadline 안에 iCloud/SAF commit이 실패해도 sandbox recovery는 남고 다음 foreground에서 재시도·다른 이름 저장을 제안한다.

## 4. 모바일용으로 재설계해야 하는 UI와 입력 시스템

### 4.1 새 입력 데이터 모델

공통 `PointerSample`은 적어도 다음 정보를 batch 단위로 전달해야 한다.

- pointer/sample ID, monotonic timestamp
- source: Apple Pencil/stylus/finger/mouse/trackpad
- tool: pen/eraser/unknown, buttons/barrel state
- document 좌표와 view 좌표
- pressure와 정규화 여부
- tilt X/Y 또는 altitude/azimuth, orientation/roll(지원되는 경우)
- hover/contact, contact ellipse
- actual/coalesced/historical/predicted/estimated 상태
- begin/update/end/cancel phase

predicted sample은 화면의 임시 preview에만 쓰고 document와 undo에는 절대 commit하지 않는다. actual sample이 오면 prediction을 교체한다. Apple estimated property update도 sample ID로 수정한다. Android `ACTION_CANCEL` 또는 `FLAG_CANCELED`를 받으면 해당 pointer가 만든 임시 stroke를 rollback한다.

문서 호환 정책은 둘로 나눈다.

- tilt/azimuth를 cursor preview에만 쓰면 `.ugu` schema는 유지한다.
- brush 재생 결과가 tilt/azimuth에 의존하면 schema를 올리고, 기존 문서는 “수직 pen/azimuth 0” 같은 명시적 기본값을 사용한다. renderer algorithm version과 desktop/mobile 왕복 호환 테스트도 함께 갱신한다.

### 4.2 Apple Pencil, S Pen, 일반 stylus

Qt `QTabletEvent`는 pressure, x/y tilt, rotation, tangential pressure와 eraser를 표현할 수 있다. 먼저 실제 기기에서 Qt가 각 필드를 충실히 전달하는지 기록·비교한다. Qt 경로가 누락하는 기능에만 native adapter를 추가한다.

Qt 6.10에는 Apple Pencil pointing device, rotation, hover 관련 iOS 개선도 들어갔다. 그렇더라도 Qt 문서가 UIKit의 coalesced/predicted/estimated sample과 Android `FLAG_CANCELED` 보존을 약속하지는 않으므로 native trace와 대조하는 PoC는 생략하지 않는다.

- iOS/iPadOS: UIKit은 force, altitude, azimuth, estimated update와 최대 240 Hz 입력에서 나온 coalesced touches를 제공한다. coalesced와 predicted touch를 모두 사용한다. `UIPencilInteraction`은 지원 모델의 double tap/squeeze와 사용자 선호 동작에 쓰고, 지속 hover의 위치·고도·방위각·roll·거리 preview는 `UIHoverGestureRecognizer` 경로로 받는다.
- Android: `MotionEvent`의 tool type, pressure, `AXIS_TILT`, `AXIS_ORIENTATION`, hover, button state와 historical points를 사용한다. 제조사에 따라 압력이 1을 넘을 수 있으므로 capability/기기별 보정 후 정규화한다. pen contact 중 `requestUnbufferedDispatch()`가 Qt 경로보다 지연을 줄이는지 비교하고, `MotionEventPredictor`의 점은 다른 prediction처럼 임시 preview에만 쓴다.
- Android의 `GLFrontBufferedRenderer` 저지연 경로는 Android 10+의 작은 OpenGL drawing region에는 후보지만 full-screen canvas를 pan/zoom하면 tearing 위험이 있어 MVP 기본 경로로 쓰지 않는다. unbuffered dispatch·prediction·dirty upload로 목표를 못 맞출 때 active-stroke 제한 영역에만 별도 PoC한다.
- Samsung S Pen의 화면 접촉 drawing은 일반 Android `MotionEvent` 경로를 기준으로 한다. Air Actions/Remote SDK는 BLE button·공중 gesture용 선택 기능이며 MVP의 필수 종속성으로 두지 않는다.
- 일반 정전식 stylus는 시스템이 finger로 보고 압력/til트를 주지 않을 수 있다. 이 경우 finger 정책으로 안전하게 degrade한다.

Apple Pencil은 iPad 계열 accessory이며 Apple의 호환 목록은 iPad 모델만 제시한다. 따라서 이는 공식 목록에 근거한 결론으로, **iPhone에서는 Apple Pencil을 지원한다고 가정하지 않는다**. iPhone의 일반 정전식 stylus는 대개 finger touch로 보이고 pressure/tilt가 없으므로 `hasPressure=false`, `hasTilt=false`인 sample로 처리한다.

| Apple 입력군 | 기대 capability | 앱 동작과 QA |
|---|---|---|
| iPhone finger/일반 정전식 stylus | Apple Pencil 없음; 보통 pressure·tilt 없음 | finger drawing 설정 또는 viewport/control 입력, 고정/default pressure curve, Pencil UI 숨김 |
| Apple Pencil 1세대 | pressure·tilt, double tap/squeeze 없음 | base iPad 계열에서 coalesced actual·pressure·tilt 검증 |
| Apple Pencil 2세대 | pressure·tilt·double tap, hover는 iPad model 의존 | double-tap preferred action을 존중하고 hover 없는 장치에서도 동일 편집 가능 |
| Apple Pencil (USB-C) | tilt, 일부 iPad의 hover, **pressure 없음** | `hasPressure=false`로 고정/default pressure를 쓰고 0 pressure로 stroke가 사라지지 않는지 검증 |
| Apple Pencil Pro | pressure·tilt·hover·double tap·squeeze·barrel roll | Pencil Pro 전용 신호는 shortcut/preview enhancement로만 쓰고 필수 command는 UI에도 유지 |

기기 이름을 business logic에 박지 않고 event/capability를 runtime에서 탐지한다. 최소 실제 기기 matrix는 iPhone 1대, base iPad+1세대 또는 USB-C Pencil, hover 가능한 iPad Pro+2세대 또는 Pro를 포함한다. 출시 지원 범위의 Pencil 세대를 조합별로 한 번씩 검증하거나 지원 제외를 명시한다.

Android 실제 기기 matrix에는 S Pen tablet, stylus-capable Android phone, 일반 USI/AES stylus tablet과 pen이 없는 low-RAM phone/tablet을 넣는다. Samsung 모델명을 특별 취급하지 않고 각 장치가 보고하는 pressure range, tilt/orientation, hover, button, eraser, cancel capability를 기록한다.

Apple Pencil에는 뒤집어 쓰는 물리 eraser tip이 없다. eraser는 화면의 tool 선택 또는 지원 모델의 double-tap preferred action으로 바꾼다. Android/S Pen/일반 stylus의 `TOOL_TYPE_ERASER`나 side button은 장치가 실제로 보고할 때만 eraser capability로 사용한다.

### 4.3 펜·터치 충돌과 팜 리젝션

입력 소유권 규칙을 명시적으로 둔다.

1. pen hover/proximity 또는 contact 중에는 finger가 stroke를 시작하지 못한다.
2. pen contact 중 두 손가락 viewport gesture는 사용자 설정에 따라 허용하되, pen stroke와 동일 pointer stream을 공유하지 않는다.
3. “손가락으로 그리기”는 기본 off 또는 명시적 설정으로 두고, on일 때도 두 번째 finger가 들어오면 stroke를 cancel/commit할지 일관된 규칙을 적용한다.
4. 두 손가락 pan/zoom/rotation이 시작되면 활성 finger stroke는 rollback하고 viewport recognizer가 exclusive ownership을 얻는다.
5. Android 13+에서는 `ACTION_POINTER_UP`/`ACTION_CANCEL`의 `FLAG_CANCELED`를 확인한다. Android 12 이하는 비-primary palm을 완전히 구분할 수 없으므로 pen proximity suppress, 접촉 면적·edge heuristic을 보조로 쓰되 사용자의 정상 touch를 과도하게 버리지 않는다.
6. 시스템 edge gesture와 safe inset에서 시작된 sequence는 canvas command를 commit하지 않는다.

### 4.4 멀티터치, 키보드와 마우스

- QML `PinchHandler` 또는 공통 gesture recognizer로 centroid 기준 pan+scale+rotation을 동시에 계산한다. viewport transform은 회전을 포함하는 하나의 matrix로 만들고, gesture 중 저해상도/기존 frame을 즉시 transform한 뒤 gesture 종료 후 정밀 rerender한다.
- pointer ID가 바뀌거나 touch cancel이 오면 gesture를 종료하고 transform을 일관되게 확정한다.
- tablet에서는 `Cmd/Ctrl+Z`, redo, save, open, copy/paste, Space-pan, Delete, brush size 같은 기존 shortcut 의미를 유지한다.
- mouse/trackpad는 hover cursor, right-click context menu, wheel/trackpad zoom, middle/Space pan을 제공한다. 터치 UI가 있다고 keyboard focus와 pointer target을 제거하지 않는다.

### 4.5 회전, Safe Area, notch, multi-window와 foldable

- 기기 종류나 고정 orientation이 아니라 **현재 window의 논리 크기와 height/width class**로 layout을 선택한다.
- Qt 6.11의 QML `SafeArea`/`QWindow::safeAreaMargins()`로 시스템 UI와 notch를 피하고, canvas 배경은 edge-to-edge로 그리되 interactive controls만 safe area 안에 둔다.
- iPad Stage Manager/split view와 Android multi-window에서 runtime resize를 정상 경로로 취급한다.
- Android foldable은 Kotlin adapter가 Jetpack WindowManager의 hinge/fold bounds, occlusion, separating/posture를 QML에 전달한다. 중요한 control과 stroke origin을 hinge 위에 놓지 않으며, 두 pane으로 분리 가능할 때만 canvas+inspector를 나눈다.
- 화면 회전 중 활성 stroke를 cancel하거나 안전하게 commit한 뒤, view 좌표를 새 window matrix에 재매핑한다. document 좌표 자체는 회전시키지 않는다.

## 5. 추천 아키텍처와 기술 스택

### 5.1 다섯 구현 방향 비교

아래 인월은 조사 범위의 네 폼팩터를 스토어 출시 품질로 만드는 총량의 계획 추정치다. “공유율”은 새 모바일 코드 중 iOS·Android가 같이 쓰는 비율이며, 앱 크기와 시작 시간은 동일 기능의 release/thinned native 앱을 기준으로 한 상대 평가다.

| 방향 | 기존 코드 재사용률 | 난이도 | 작업 규모 | iOS·Android 공유율 | 장기 유지보수 |
|---|---:|---:|---:|---:|---|
| **1. Qt for iOS/Android 직접 이식 + 새 QML UI** | 코어 70~85%, 기존 UI 0~15%, 전체 45~60% | 높음(4/5) | **21~33 인월** | **75~90%** | 코어·모바일 UI가 한 벌이라 가장 균형이 좋다. 단 Qt/QRhi 버전 pin과 native adapter는 유지해야 한다. |
| 2. 핵심 C++만 공유 + SwiftUI/Compose | 코어 40~60%, 전체 25~40% | 매우 높음(5/5) | 32~48 인월 | 35~50% | UI·렌더 동작·접근성·QA를 두 벌 유지한다. 각 플랫폼 팀이 충분할 때만 타당하다. |
| 3. C++ 문서·렌더 공유 + native UI | 코어 60~75%, 전체 35~50% | 매우 높음(5/5) | 27~42 인월 | 55~70% | option 2보다 렌더 일관성은 높지만 두 native surface와 input bridge가 가장 복잡한 경계가 된다. 차선책이다. |
| 4. Flutter/React Native 등 | 코어 25~45%, 전체 15~30% | 매우 높음(5/5) | 28~45 인월 | 새 UI 75~90% | 프레임워크 UI는 공유되지만 Qt/C++ engine과 FFI/native view/plugin을 함께 운영한다. 현재 자산과 맞지 않는다. |
| 5A. 데스크톱까지 QML 한 UI로 통합 | 코어 70~85%, Widgets UI 0~10% | 매우 높음(5/5) | 28~42 인월 | 제품 UI 80~95% | 코드 수는 줄지만 데스크톱 UX 전체를 동시에 재작성하고 모든 폼팩터에 절충이 생긴다. |
| **5B. 데스크톱 Widgets + 별도 모바일 QML** | 방향 1과 동일 | 높음(4/5) | **21~33 인월** | 모바일 UI 75~90% | 공통 코어 위에 두 UI family를 둔다. 제품별 UX가 명확하고 기존 데스크톱 회귀 위험이 가장 작다. **추천** |

| 방향 | 렌더링 성능 | 메모리 | 앱 크기 | 시작 시간 | 플랫폼 기능 연동 | 스토어 배포 적합성 |
|---|---|---|---|---|---|---|
| **1 / 5B** | CPU renderer의 결과 일관성·tile 이점을 유지한다. Qt/native event 전달과 texture upload를 최적화해야 한다. | 기존 full-frame/DOM 정책 때문에 **중~고위험**이나 한 코어에서 해결 가능 | Qt runtime·plugins로 **중~대** | native보다 대체로 느린 **중간**; QML/plugin 수에 좌우 | 표준 기능은 Qt, 고충실도 입력·문서는 얇은 native bridge로 **중간** | Android의 GPL 소스·직접 APK/F-Droid 배포와 잘 맞는다. iOS 공개 배포는 별도 **GPL/Qt 라이선스 P0**다. |
| 2 | 플랫폼별 최적 surface를 쓸 잠재력은 가장 높지만 동일 stroke 결과 구현 비용이 크다. | native 정책에 최적화 가능하나 bridge copy와 이중 cache 위험 | Qt Core/Gui를 유지하면 **중간**, de-Qt 재작성까지 해야 **소~중** 가능 | Qt runtime을 유지하면 **중간**, de-Qt일 때만 native 수준의 **빠름** 기대 | SwiftUI/Compose만으로 부족한 canvas는 UIKit/View bridge가 필요하나 전체적으로 **좋음** | 기술적으로 자연스럽지만 일정·동작 불일치 위험이 큼 |
| 3 | 공유 CPU renderer로 품질은 일정하나 SwiftUI/UIKit·Compose/View texture surface 경계가 복잡하다. | cross-language image copy를 없애야 하며 **중간** | C++ engine 포함 **중간** | **빠름~중간** | native chrome은 좋고 C++ surface는 별도 작업이므로 **중~상** | 양호. 두 플랫폼별 archive와 native bridge QA 부담 |
| 4 | 일반 UI는 충분하나 저지연 drawing surface는 custom engine/native texture가 필요하다. | Flutter/RN runtime + C++/Qt 또는 이중 engine으로 **고위험** | **중~대** | **중간~느림** | plugin이 없는 stylus·SAF·document workflow는 native code 필요 | 제출 가능하나 SDK privacy manifest/policy와 plugin 공급망 부담 |
| 5A | 하나의 QML renderer를 최적화할 수 있다. | 방향 1과 같음 | 방향 1과 같음 | 방향 1과 같음 | 방향 1과 같음 | 가능하나 모바일과 무관한 desktop 재작성 위험이 출시를 늦춤 |

옵션 1의 “직접 이식”은 **현재 Widgets 화면을 그대로 빌드한다는 뜻이 아니다**. Widgets를 억지로 터치 UI로 만들면 컴파일 성공과 제품 적합성을 혼동하게 된다. 재사용 대상은 Qt/C++ 코어이고, 모바일 view는 QML로 새로 만든다.

네이티브 UI를 고르는 옵션 2·3도 현재 `QImage`/`QPainter`/`QObject` 중심 코어를 유지하는 한 Qt 라이선스 문제를 없애지 않는다. Qt를 완전히 제거하려면 문서·renderer·serializer의 public type과 상당한 구현을 다시 써야 하므로 별도 프로젝트 규모다. UI framework 선택으로 법률 게이트가 해결된다고 가정하지 않는다.

옵션 2·3에서 SwiftUI를 쓰더라도 고주파 Pencil canvas는 UIKit view/event와 결합하는 편이 현실적이며 C++ 경계에는 Objective-C++ façade가 필요하다. Swift의 C++ interop도 가능하지만 예외·일부 template/rvalue reference·container copy 등의 제약이 있어 현재처럼 Qt 타입이 넓게 노출된 객체 그래프를 직접 연결하는 주 경계로 삼지 않는다. **추천안에는 Swift와 SwiftUI가 필요 없다.** UIKit/Objective-C++은 전체 UI가 아니라 Pencil·문서·공유·scene API용 얇은 iOS adapter다.

Flutter는 자체 C++ engine/Impeller와 Dart runtime, React Native는 JSI/Fabric와 native platform module을 도입한다. 어느 쪽도 기존 Qt renderer를 자동 재사용하지 않는다. Qt까지 유지하면 runtime과 build graph가 겹치고, Qt를 제거하면 QImage/QPainter·QObject·QString에 결합된 코어를 대대적으로 바꿔야 한다. 이 프로젝트에 이를 상쇄할 이점은 없다.

### 5.2 권고 구조

```text
Desktop                                      Mobile shared
Qt Widgets UI                               Qt Quick/QML adaptive UI
       │                                             │
       ├────────────── app/session façade ───────────┤
       │                                             │
       └──── shared Qt/C++ core ─────────────────────┘
             document · command · history
             serializer/codec · recovery model
             CPU renderer · tile preview · export
                         │
                  QQuickRhiItem presenter
                  ┌──────────┴──────────┐
             iOS/iPadOS             Android
             Metal/QRhi              Vulkan or GLES/QRhi
             UIKit + ObjC++          Kotlin/Java + JNI
             thin adapters           thin adapters
```

기술 스택은 다음처럼 고정한다.

- 공통: C++23, **Qt Community Edition** 6.11.x의 Core/Gui/Concurrent, 기존 spdlog/libwebp, CMake. 공개 build에는 GPL/LGPL로 제공되는 module만 사용하고 Qt patch 버전과 대응 source를 모든 개발·CI·배포 환경에서 정확히 pin한다.
- Android 공개판은 이미 GPL-3.0-or-later인 Ugurugu와 맞춰, 사용 Qt module이 제공하는 **GPLv3 option을 기본 선택**으로 license manifest에 기록한다. LGPL option을 선택해야 하는 component가 있으면 source·relink·install 의무를 별도 항목으로 검증한다.
- 모바일 UI: Qt Quick/QML와 Qt Quick Controls. `PointerHandler`/`PinchHandler`, QML `SafeArea`, keyboard focus/shortcut을 사용하되 gesture ownership은 공통 C++ policy로 둔다.
- canvas 표시 1차 후보: `QQuickRhiItem`과 QRhi. 기존 QImage BGRA texture·dirty upload·quad transform을 옮긴다. `QQuickRhiItem`은 software scene graph가 없고 render thread가 분리될 수 있으므로 `synchronize()` 경계와 immutable frame handoff를 지킨다. offscreen surface 비용이 기준을 넘으면 방향 3의 native surface 또는 더 직접적인 scene graph 경로를 재평가한다.
- iOS/iPadOS: Metal backend, Qt iOS Xcode generator, Objective-C++ `.mm` + UIKit adapter. `UIDocumentPickerViewController`/`UIDocument` 또는 coordinated URL I/O, `UIActivityViewController`, UIScene/lifecycle, Pencil API를 연결한다. Swift/SwiftUI는 baseline 의존성에서 제외한다.
- Android: Vulkan을 우선 후보로 측정하되 startup capability/검증된 device profile을 통과한 경우에만 선택하고 나머지는 OpenGL ES로 시작한다. 초기화 실패는 다음 실행에서 GLES를 선택하도록 기록한다. QtActivity를 최소 확장하고 Kotlin/Java + `QJniObject`/JNI로 MotionEvent, ContentResolver/SAF, Sharesheet, WindowManager fold, lifecycle/memory 신호를 연결한다.
- Qt Android는 Qt thread와 Android UI thread가 분리되고 여러 app Activity를 일반적인 native architecture처럼 운영하는 것을 지원하지 않는다. UI-thread 호출을 adapter가 marshal하고, 앱 내부 화면을 여러 Activity/Compose navigation으로 나누지 않으며 외부 system picker activity의 result만 받아들인다.
- 테스트: 기존 Qt Test core suite와 pixel golden을 host·mobile core target에서 재사용하고, XCTest/XCUITest 및 Android instrumentation/UI Automator 계층에는 bridge·lifecycle·file round-trip만 둔다. stylus 품질은 실제 기기 trace/replay와 수동 drawing protocol을 병행한다.

Qt 6.11 공식 지원선은 iOS 17 이상, Android API 28~36이다. 2026년 App Store 업로드는 Xcode 26과 iOS/iPadOS 26 SDK가 필요하므로 **SDK 26으로 빌드하되 deployment target은 iOS 17**로 두는 것이 초기안이다. Android는 예상 출시 시점의 Play 요구에 맞춰 **targetSdk 36, minSdk 28**을 초기안으로 삼는다. 이는 1단계에서 제품 대상 사용자와 실제 Qt binary 호환을 확인한 뒤 확정한다.

### 5.3 고해상도 canvas와 저사양 기기 정책

4096² RGBA surface 한 장은 64 MiB다. base, active stroke, composite, selection, GPU texture와 여러 animation frame을 동시에 full resolution로 가지면 작은 tablet의 budget을 즉시 넘을 수 있다. 모바일 renderer에는 다음 순서를 계약으로 둔다.

1. document vector/command data가 권위 있는 원본이며 display cache는 언제든 폐기·재생성할 수 있다.
2. 화면에는 현재 frame의 필요한 tile과 display-scale surface를 우선한다. full-resolution surface 수를 명시적으로 예약하고 reservation 실패 뒤 allocate하지 않는다.
3. CPU image, decoded raster, GPU texture, history, serializer/export working set을 별도 계정으로 추적하되 전체 ceiling을 함께 적용한다.
4. memory warning/low-RAM에서는 warm frame → offscreen layer → decoded asset → 오래된 undo 순으로 줄이고, 현재 stroke와 최신 recovery journal은 마지막까지 보존한다.
5. animation export는 frame을 render→encode→release하는 pipeline으로 바꾸고 모든 `QImage`를 vector에 모으지 않는다. serializer도 전체 JSON DOM/bytes 이중 보관을 제거한다.
6. worker 수는 CPU core 수가 아니라 available memory, thermal state, foreground 상태와 job reservation으로 제한한다. background에서는 신규 preview/export를 시작하지 않는다.
7. 저사양 Android에서는 preview scale·warm frame 수·undo byte budget·동시 export를 낮춘다. 파일 형식과 최종 render 의미는 바꾸지 않으며, full-resolution export가 안전하지 않으면 작업 전 예상 메모리를 보여주고 크기 축소 또는 나중에 실행하도록 한다.
8. QQuickRhiItem의 color buffer pixel 수는 논리 크기 대비 device-pixel ratio의 제곱으로 늘어난다(DPR 2면 네 배). fixed/limited texture size와 viewport tile을 사용하고 hardware maximum, allocation 실패와 device loss를 정상 fallback으로 처리한다.
9. `QQuickRhiItem`은 offscreen color buffer를 만든 뒤 Qt Quick scene에 합성한다. 현재 QImage 업로드용 source texture와 별도의 full-surface render target·scenegraph buffer가 동시에 존재할 수 있으므로 CPU/GPU allocation trace를 합산한다. 이 peak가 budget을 넘으면 QQuickRhiItem을 확정하지 않는다.
10. 이미지 import는 header의 dimension·format·orientation을 먼저 검증하고 decode budget을 예약한다. 압축 파일 크기만 보고 신뢰하거나 무제한 full-resolution decode를 시작하지 않는다.

`minSdk 28`은 OS API 지원선이지 모든 API 28 기기의 RAM/GPU를 지원한다는 뜻이 아니다. 사전 측정에서 4K 단일 frame을 데이터 손실 없이 처리할 최소 RAM·GPU/texture 조건을 공개 지원선으로 정한다. 목표 기기가 이를 못 만족하면 문서를 조용히 downsample하지 말고 tile pipeline을 구현하거나 해당 hardware를 명시적으로 제외한다.

Metal/Vulkan/OpenGL ES는 같은 QRhi presenter contract 아래 둔다. iOS는 Metal을 주경로로, Android는 Vulkan의 실제 지원과 driver 안정성을 확인한 뒤 다음 실행에 적용되는 GLES fallback을 유지한다. graphics API는 첫 Qt Quick window를 만들기 전에 선택해야 한다는 제약을 설계에 반영한다. GPU 선택만으로 CPU brush가 빨라지는 것은 아니므로 texture upload, dirty area, frame pacing, CPU raster 시간을 따로 측정한다.

### 5.4 권고안을 바꾸는 조건

권고는 Qt/QML을 무조건 고수한다는 뜻이 아니다. 사전 검증의 전환 기준을 미리 정한다.

- `QTabletEvent`와 public/native injection 경로로 mandatory Pencil/MotionEvent sample·cancel을 보존할 수 있고 private Qt platform plugin fork가 필요 없으면 방향 1/5B를 유지한다.
- UIKit event를 Qt Quick window에 안정적으로 연결하려면 지속적인 private QPA patch가 필요하거나, Android 두 대표 GPU family에서 QQuickRhiItem이 합의한 frame/latency 기준을 bounded optimization 후에도 넘으면 **방향 3(native UIKit/Objective-C++ 및 Android View UI + shared C++ document/renderer)**으로 전환한다.
- 플랫폼별 팀을 장기 유지할 예산이 없으면 방향 3 전환은 기능 범위 축소와 함께 승인한다. SwiftUI/Compose 두 벌을 추가하는 방향 2로 자동 확대하지 않는다.
- Ugurugu product tree에는 Qt Educational License나 commercial evaluation artifact를 사용하지 않고 local PoC까지 Community Edition으로 통일한다. Android는 module별 license audit와 source-package rehearsal로 진행하고, iOS 외부 배포는 Qt Company의 서면 답변과 독립적인 법률 검토가 승인할 때만 연다.
- iOS의 Qt/GPL 배포 경로가 해결되지 않으면 UI framework만 바꾸지 말고 Qt type을 포함한 core를 표준 C++·허용적 라이선스 library로 다시 만드는 비용을 산정한다. UIKit/Objective-C++로 shell만 바꾸고 `QImage`·`QPainter`를 남기는 것은 해결책이 아니다.

## 6. iOS·Android 공통 코드와 플랫폼별 코드의 경계

### 6.1 공통으로 유지할 코드

- 문서 graph, layer/frame/selection/mask 연산과 command/controller
- undo/redo delta, history budget, dirty/revision 판단
- `.ugu` schema, validation, canonical raster, legacy import와 codec
- CPU reference renderer, incremental tile stroke, composition plan, display-scale replay
- brush/stabilizer와 정규화된 `PointerSample` 이후의 stroke builder
- viewport transform 수학과 gesture arbitration 상태 machine
- recovery journal/checkpoint 형식과 generation/conflict 규칙
- export job model, GIF/WebP encoder, cancellation/progress/error model
- mobile QML component, adaptive navigation, tool/layer/timeline view model
- core logging, deterministic test fixtures, pixel golden과 input trace replay

### 6.2 iOS/iPadOS 전용 코드

- explicit App ID/entitlement와 UIScene/app lifecycle adapter
- UIKit touch event에서 coalesced/predicted/estimated Pencil sample을 수집하는 Objective-C++ adapter
- `UIPencilInteraction`의 double tap/squeeze와 `UIHoverGestureRecognizer`의 hover pose를 capability별로 전달하는 adapter
- security-scoped URL/bookmark, File Provider/iCloud Drive, `NSFileCoordinator`/`UIDocument` 기반 commit
- `UIDocumentPickerViewController`, Photos/import, `UIActivityViewController`
- safe area/trait/keyboard/pointer 상태와 memory warning 전달
- Xcode asset catalog, Info.plist, PrivacyInfo.xcprivacy, signing/archive 설정

### 6.3 Android 전용 코드

- QtActivity lifecycle/UI-thread adapter와 JNI/QJniObject 경계
- `MotionEvent`의 history, tool/axis/button/hover, `ACTION_CANCEL`/`FLAG_CANCELED`를 batch로 옮기는 adapter
- SAF `content://` URI, persistable grant, ContentResolver descriptor/stream, Photo Picker
- Sharesheet, `FileProvider`, incoming `ACTION_VIEW`/`ACTION_SEND`
- WindowInsets, WindowManager window/fold/hinge 정보, keyboard/mouse 상태
- memory class/low-RAM/trim·thermal 신호와 Vulkan capability/driver fallback
- AndroidManifest, Gradle package overlay, adaptive icon, AAB/ABI/signing 설정

JNI와 Objective-C++ 경계에는 개별 sample이나 pixel마다 호출하지 않는다. 입력은 batch, frame은 immutable native/shared buffer 또는 QRhi texture upload unit, 문서는 opaque handle + command DTO로 전달한다. 플랫폼 view가 `Document`의 내부 Qt container를 직접 소유하지 않게 해 수명과 thread 계약을 좁힌다.

## 7. 스마트폰과 태블릿의 UI 전략

### 7.1 adaptive layout

“iPad인가”, “Android tablet인가”를 한 번 판단해 고정하지 않고 현재 window 크기·방향·safe inset·hinge를 입력으로 layout mode를 매번 계산한다. Android 공식 window size class를 공통 QML breakpoint의 초기값으로 삼되 iOS trait와 실제 사용성 측정으로 조정한다.

최종 배포는 iPhone+iPad를 지원하는 **하나의 universal iOS app**과 phone+tablet+foldable을 지원하는 **하나의 Android package**를 권장한다. 문서 형식·설정·스토어 listing을 나누지 않고 현재 window에 따라 topology를 바꾼다. 초기 internal target은 iPad 전용으로 좁힐 수 있지만 phone UI 단계가 끝나면 같은 product identifier의 universal build로 검증한다.

| 현재 window 폭 | 화면 구성 | 주요 동작 |
|---|---|---|
| Compact, `<600dp/pt` | canvas 우선, 상단 최소 command bar, 하단 tool strip, inspector/layer/timeline은 한 번에 하나의 full-height sheet | 한 손 도달성, sheet 뒤 canvas gesture 차단, 가로 모드에서만 보조 pane 허용 |
| Medium, `600~839` | 접을 수 있는 좌측 tool rail + canvas, inspector는 overlay 또는 sheet | 작은 iPad split view, 큰 phone, 세로 tablet과 foldable 대응 |
| Expanded, `840~1199` | tool rail + canvas + 선택적 우측 inspector, 하단 timeline | 표준 tablet editing mode |
| Large 이상, `≥1200` | canvas를 중심으로 양측 panel과 지속 timeline, keyboard/mouse command 노출 | 큰 tablet·외부 display·DeX류 desktop window 대응 |

controls는 플랫폼의 최소 touch target과 Dynamic Type/font scale을 지키며, hover와 keyboard focus ring을 별도로 제공한다. canvas는 edge-to-edge 배경을 허용하지만 brush/layer/timeline hit target은 safe area·system gesture inset·hinge를 침범하지 않는다.

### 7.2 태블릿 UI

- 왼쪽에는 brush/tool rail, 오른쪽에는 layer·brush inspector, 아래에는 frame timeline을 둔다. 각각 독립적으로 접을 수 있고 canvas를 최대화하는 “focus mode”를 제공한다.
- Pencil/S Pen에는 canvas drawing을 우선 배정하고, 손가락 두 개는 viewport navigation, 한 손가락은 controls와 명시적으로 허용한 finger drawing에 사용한다.
- keyboard·mouse가 연결되면 menu/shortcut 도움말, hover cursor, context menu와 정밀 slider 조작을 노출한다. touch controls는 사라지지 않는다.
- split view/Stage Manager/multi-window로 폭이 줄면 desktop dock을 압축하는 대신 medium/compact topology로 즉시 재배치한다.

### 7.3 스마트폰 UI

- 기본 화면은 canvas와 가장 자주 쓰는 brush/eraser, size/color, undo/redo만 유지한다. layer, timeline, document/export는 modal navigation 또는 bottom sheet로 보낸다.
- panel 여러 개를 동시에 띄우지 않고, sheet를 닫으면 focus와 viewport가 보존된다. canvas 위 장시간 작업을 위해 tool strip 위치를 좌우 손잡이 설정으로 바꿀 수 있게 한다.
- portrait는 한 손 command와 세로 canvas, landscape는 임시 side inspector를 사용한다. orientation 변경이 편집 session을 재생성하거나 undo stack을 잃게 해서는 안 된다.
- phone은 태블릿 UI의 축소판이 아니라 동일 view model을 쓰는 다른 navigation topology다. 기능 parity는 유지하되 동시에 보이는 정보량과 command 진입 방식만 다르게 한다.

### 7.4 첫 MVP 플랫폼: iPad 대 Android 태블릿

| 기준 | iPad 먼저 | Android 태블릿 먼저 |
|---|---|---|
| 현재 제품 적합성 | 큰 canvas, panel, timeline과 잘 맞음 | 큰 화면에는 맞지만 제조사별 화면·RAM·펜 편차가 큼 |
| 입력 검증 | Apple Pencil의 명확한 API와 비교적 작은 기기 matrix | S Pen을 포함한 MotionEvent 표준을 검증하나 vendor pressure/palm/hover 편차가 큼 |
| 그래픽 | Metal 단일 주경로 | Vulkan/GLES와 GPU driver fallback을 동시에 검증해야 함 |
| 저장·수명주기 | Files/iCloud provider와 UIScene가 복잡하지만 경계가 비교적 일관됨 | SAF provider 편차, activity/process death, low-RAM이 더 가혹함 |
| 기존 환경 | macOS Qt/Xcode 경험과 Apple 계정을 활용 가능 | Android toolchain·Gradle·Play 운영을 새로 구축해야 함 |
| 위험 발견 | core canvas UX를 빠르게 증명하기 좋음 | 최악의 fragmentation·memory 문제를 일찍 발견하기 좋음 |
| 예산 0원 공개 | App Store/TestFlight의 GPL·Qt 정적 링크 경로를 먼저 서면 승인받아야 함 | 최대 20대는 닫힌 무료 Limited alpha. public APK는 무료지만 20대 밖 설치에 advanced flow/ADB 마찰이 생길 수 있고, 저마찰 Full Distribution은 US$25 |
| 배포 게이트 | GPL/App Store 조건 검토가 특히 중요 | GPL/LGPL 준수 package와 재빌드·재설치 가능성을 준비해야 하나 공개 경로를 직접 통제할 수 있음 |

제품·기술 조건만 보면 iPad가 더 빠른 첫 canvas 검증 대상이다. 그러나 **학생·예산 0원·무료 공개라는 실제 제약을 포함하면 Android 태블릿을 첫 MVP 플랫폼으로 추천한다.** S Pen 1대, 일반 USI/AES stylus 1대, 저사양 Android tablet 1대를 소유하거나 학교·지인에게 빌릴 수 있어야 한다. 이를 primary matrix로 삼아 input trace, QRhi Vulkan/GLES, 4K memory와 SAF를 닫고 최대 20대의 닫힌 무료 alpha를 먼저 검증한다. 이후 APK와 같은 revision의 전체 소스·빌드 방법·license notice를 public release page에 올리되 20대 밖 설치의 advanced-flow 마찰을 고지한다. F-Droid는 즉시 배포를 보장하는 우회 채널이 아니라 별도 inclusion·source-build 심사를 통과해야 하는 후속 후보다.

iPad는 포기하지 않는다. 보유 계정으로 본인 등록 기기에 개발 서명한 **local shadow PoC**를 유지해 Apple Pencil, Metal, UIKit/Objective-C++ adapter와 `.ugu` 호환성을 검증한다. 다만 타인에게 제공하는 TestFlight와 App Store build는 Qt/GPL 배포 경로가 서면 승인되기 전까지 milestone 완료 조건에 포함하지 않는다.

## 8. 최소 기능 모바일 버전의 범위

### 8.1 포함 범위

- 새 문서 생성, 최근 문서, `.ugu` 열기·atomic 저장·다른 이름 저장
- brush와 eraser, color/size/opacity, 지원 hardware의 pressure, 안정화. Apple Pencil은 UI/double-tap tool 전환, Android는 보고된 eraser/side-button capability를 사용한다. tilt/azimuth는 수집·cursor/trace까지 지원하고 brush 결과에 반영할지는 schema 결정 뒤 확정
- pan, pinch zoom, rotation, fit/reset, hover cursor
- undo/redo와 기본 selection, layer 추가·삭제·순서·가시성·opacity
- frame 추가·삭제·이동, 기본 wobble/animation playback과 timeline
- PNG/JPEG 이미지 가져오기, 현재 frame PNG/JPEG 내보내기
- memory-bounded animated GIF 한 가지와 시스템 share; animated WebP와 고급 export option은 후속
- per-document autosave/recovery, foreground/background·회전·window resize 복구
- Android tablet keyboard/mouse shortcut과 기본 접근성 label/focus/font scaling
- Android SAF open/create, Sharesheet, S Pen·일반 stylus drawing
- iPad local PoC의 Apple Pencil·Metal·UIKit 입력과 동일 `.ugu` 열기. Files/iCloud Drive와 share sheet는 iOS 공개 경로 승인 뒤 release 범위로 승격
- 오류가 있는 파일, read-only/provider 권한 만료, 저장 공간 부족에 대한 데이터 보존형 오류 처리

### 8.2 MVP에서 연기할 범위

- desktop dock/plugin/preset 관리 UI의 완전한 parity와 사용자 정의 shortcut 편집
- 모든 advanced blend/mask/group 작업의 모바일 전용 편집 UI
- WebP animation의 모든 고급 option과 여러 animation을 동시에 export하는 queue
- Pencil Pro squeeze·Samsung Air Actions를 활용한 특수 command. 입력을 받는 기반은 두되 필수 workflow로 삼지 않는다.
- 실시간 cloud sync, 공동 편집, version browser와 provider 간 conflict merge
- 완전한 GPU brush/compositor 재작성, 무제한 canvas, 여러 문서를 동시에 full-resolution으로 유지
- phone 전용 UI와 foldable dual-pane polish는 tablet vertical slice 뒤에 진행
- 외부 TestFlight와 App Store 출시는 Qt/GPL 배포 경로가 서면 승인될 때까지 연기

“태블릿용 MVP” 단계에서는 Android sandbox와 internal APK로 이 편집 loop를 먼저 세운다. 외부 tester에게 배포 가능한 MVP의 완료는 이후 SAF·공유, 성능, 플랫폼 QA와 GPL/LGPL release package를 통과한 시점이다. iPad local PoC는 같은 core의 회귀 검증 대상이지 Android MVP의 blocker가 아니다.

## 9. 단계별 개발 로드맵

작업량은 엔지니어 인월이며 일부가 병렬화된다. 법률·디자인·QA의 파트타임 지원은 포함하지만 외부 심사 대기와 라이선스 협상 시간은 포함하지 않는다.

| 단계 | 주요 작업 | 산출물 | 규모 | 선행 관계 |
|---|---|---|---:|---|
| **사전 기술 검증** | Qt Community Android release와 GPL/LGPL source·rebuild package 결정, Educational/commercial artifact 부재 확인, 실제 Android tablet 3종과 Pencil iPad 접근 확보, Qt 6.11/Xcode 26/Android API 36 toolchain, QQuickRhiItem Metal·Vulkan·GLES, Qt-vs-native stylus trace, 4K serialize/export peak, thinned app size·cold start, iOS local signed device build·Android signed APK skeleton | Android 20-device 무료 alpha go/no-go, 광범위 배포 비용/advanced-flow 기록, iOS 공개 보류/승인 기록, event capability matrix, 성능·메모리 baseline, 확정 지원 OS/ABI/backend | 1.5~2.5 | 없음; P0 |
| **공통 코어 분리** | macOS `APPLE` 분기 해체, core/session/UI 경계, path-free stream/document handle, platform service interface, mobile memory/scheduler policy, core-only mobile build·CI | iOS/Android에서 빌드·테스트되는 UI 비의존 core와 adapter contract | 2~3.5 | 배포·Qt 선택 승인 |
| **렌더링 및 입력 이식** | QQuickRhiItem presenter, immutable frame handoff, dirty upload, `PointerSample`, UIKit/MotionEvent batch adapter, gesture state machine, cancel/prediction, rotation transform | 실제 iPad/Android tablet에서 draw·pan·zoom·rotate하는 canvas vertical slice | 3.5~5 | core façade |
| **태블릿용 MVP** | adaptive QML tablet shell, brush/color/layer/timeline, undo/redo/playback, sandbox recovery, Android keyboard/mouse/accessibility; iPad local shadow build 유지 | Android tablet internal editing MVP와 동일 core의 iPad local smoke build | 3~4.5 | 렌더·입력 |
| **스마트폰 UI 적용** | compact navigation, sheets, portrait/landscape, one-hand tool layout, resize state retention, phone keyboard/mouse 기본 | iPhone·Android phone 기능 parity UI | 1.5~2.5 | shared view model·tablet flow |
| **파일 시스템 및 공유 기능** | QIODevice/stream serializer, SAF URI/grants 우선, incoming open, image import, bounded export, share, per-document recovery; iOS 승인 뒤 coordinated commit·iCloud/Files 추가 | Android provider round-trip과 OS share가 되는 beta, 조건부 iOS local 검증 | 2~3 | document handle/core codec |
| **성능 최적화** | streaming JSON/export, tile/cache budget, mobile RAM tier, reservation/cancel, worker·thermal policy, texture size/failure, backend fallback, package/QML startup trimming | 합의한 frame/input/start/RSS 기준을 충족한 release build | 2~4 | end-to-end 기능 |
| **플랫폼별 품질 검증** | Pencil/S Pen/general stylus, low-RAM/GPU, rotation/multi-window/fold, keyboard/mouse, accessibility/localization, offline/provider failure, soak/process death, upgrade/file compatibility | 지원 matrix 결과, P0/P1 결함 0, release candidate | 3~5 | 기능·성능 안정화 |
| **스토어 배포 준비** | Android signing, source tag, license/source/rebuild bundle, F-Droid metadata 또는 직접 APK, 선택적 Play track·Data Safety·native symbols; iOS 승인 시에만 App ID·profile·TestFlight·App Store 절차 수행 | Android 공개 release와 재현 가능한 source package, 조건부 Apple 심사 | 1.5~2.5 | QA RC; 일부 계정 작업은 조기 병행 |
| **합계** |  | 네 폼팩터 출시 품질 | **20~32.5 인월**, 계획상 **21~33 인월** |  |

기준 staffing은 Qt/C++·render 2명, iOS/Android native bridge 1명, QML/product UI 1명을 가정한다. 예산 0원의 1인 학생 개발에서는 이 구성을 흉내 내려 하지 않고 Android 태블릿의 한 vertical slice를 순차적으로 완성한다. phone UI와 iOS 공개판을 동시에 진행하지 않으며 디자인·실기기 QA는 학교 lab, 오픈소스 기여자와 공개 beta의 도움을 받더라도 최종 release 책임은 개발자가 유지한다. 기준 팀 3명은 약 9~13개월, 4명은 7~10개월이며 1인 파트타임의 전체 범위는 다년 일정으로 본다.

## 10. 단계별 완료 조건과 검증 방법

| 단계 | 완료 조건 | 검증 방법 |
|---|---|---|
| 사전 기술 검증 | Qt Community만으로 만든 Android artifact의 GPL/LGPL source·build·재서명·설치 경로가 문서화되고 Educational/commercial artifact가 섞이지 않는다. 무료 Limited Distribution 장치 한도와 광범위 배포 비용·advanced flow가 결정 기록에 포함된다. iOS 공개 상태는 “서면 승인” 또는 “보류”다. 필요한 실제 tablet 접근과 입력 adapter·4K baseline이 확보된다. | module/dependency/license/기여자 self-audit, clean Linux Android signed APK source rebuild, iPad local signed artifact, Pencil/S Pen/일반 pen trace diff, Metal/Vulkan/GLES golden, profiler·RSS/thermal log |
| 공통 코어 분리 | core target이 Widgets/AppKit/Sparkle/Win API 없이 iOS arm64+simulator와 Android arm64-v8a+x86_64에서 빌드된다. path 없이 memory/device stream round-trip이 된다. | 모든 기존 core test, schema 1~13 fixture, malformed corpus, host/mobile CI, forbidden dependency scan |
| 렌더링 및 입력 이식 | pressure/eraser/tilt/azimuth가 capability에 맞게 전달된다. predicted stroke는 실제 sample로 교체되고 cancel 뒤 ghost/undo 항목이 남지 않는다. 두 손가락 transform과 pen이 충돌하지 않는다. | recorded input replay, 10분 고속 stroke에서 sample/order 손실 검사, hand/palm protocol, transform property test, screenshot/pixel diff, render-thread sanitizer/soak |
| 태블릿용 MVP | Android tablet에서 create→draw→layer/frame edit→undo/redo→playback→background kill→recover vertical slice가 가능하다. 모든 필수 기능을 stylus 없이 touch/mouse로도 수행한다. iPad local shadow build가 같은 `.ugu`를 연다. | S Pen·일반 stylus·low-RAM tablet, keyboard/mouse, TalkBack, multi-window와 30분 editing session; iPad Pencil capability·Stage Manager·VoiceOver local smoke 및 cross-open |
| 스마트폰 UI 적용 | compact portrait/landscape에서 가려진 필수 command나 겹친 safe-area control이 없고, 회전·resize 뒤 document/tool/viewport state가 보존된다. | iPhone/Android small·large phone screenshot matrix, font scale, notch/cutout, one-hand usability, rotation loop, keyboard appearance test |
| 파일 시스템 및 공유 | Android local/SAF provider에서 open/edit/save-as/reopen이 byte·pixel 의미를 보존한다. stale/revoked/read-only/offline/저장 공간 부족이 원본을 손상하지 않는다. iOS 승인이 있으면 Files/iCloud를 같은 contract로 추가한다. | SAF provider별 fault injection, persisted grant 재실행, commit 중 process kill, conflict/copy test, PNG/JPEG/GIF decode·share round-trip; 조건부 security-scope test |
| 성능 최적화 | 합의한 reference devices에서 frame/input/start/memory 기준을 충족하고 low-RAM 장치에서 OOM·OS kill 없이 degrade한다. | release build cold/warm benchmark, 1/4/60 frame 4K corpus, 30분 thermal soak, memory warning/trim injection, GPU loss/resize, AAB/IPA size report |
| 플랫폼별 품질 검증 | Android 지원 OS·폼팩터·입력 matrix에서 P0/P1 결함 0, 저장 손실 0이며 beta crash가 목표 이하다. upgrade와 desktop↔mobile file compatibility가 통과한다. iPad는 local regression matrix를 유지한다. | device farm + 실제 stylus lab, fuzz/regression/golden, background/process-death loop, accessibility/localization, 직접 APK/F-Droid/closed-track telemetry와 iPad local soak |
| 스토어 배포 준비 | Android 서명·privacy·metadata와 GPL/LGPL 대응 source가 production artifact와 일치하며 직접 APK/F-Droid 또는 선택한 Play channel에서 검증된다. Apple은 배포 승인 뒤에만 동일 수준의 TestFlight/App Store gate를 적용한다. | clean CI APK/AAB와 source rebuild, signature/license inspection, native symbolication, F-Droid recipe 또는 Play upload validation; 조건부 Xcode archive·dSYM·review checklist |

사전 검증에서 수치 baseline을 얻은 뒤 product/engineering이 최종 SLO를 승인한다. 시작점으로 사용할 **잠정 기준**은 다음과 같다.

- reference 60 Hz 장치에서 interaction 중 frame time p95 ≤16.7 ms; 120 Hz iPad Pro의 stretch 목표는 p95 ≤8.3 ms
- 240 Hz급 입력 trace 10분 동안 actual/coalesced sample 유실·순서 역전 0; end-to-end stroke latency는 해당 플랫폼 native reference 대비 한 display frame 이상 악화되지 않음
- cold start p95 ≤2.5초, warm document resume p95 ≤1초. 앱 크기는 기능별 plugin/asset 기여도가 기록되고 store download size 예산을 1단계에서 확정
- autosave는 pen-up 뒤 idle 5초 이내 durable recovery point를 만들고, background callback은 이미 준비된 작은 journal/checkpoint flush만 수행
- 저사양 지원 기기에서 4096² 단일 frame 편집이 가능하고, cache·history·preview를 순서대로 축소한 뒤에도 OOM/process kill 없이 사용자에게 해상도/export 제약을 설명

이 기준은 측정 전 계약값이지 현재 코드가 충족한다는 주장이 아니다. 120 Hz는 stretch 목표이며 MVP 출시를 막는 기준은 60 Hz reference와 입력 정확성이다.

## 11. 주요 기술적 위험

| 우선순위 | 위험 | 가능성 / 영향 | 조기 증거와 대응 |
|---:|---|---|---|
| P0 | Android release에 불완전한 GPL/LGPL source package나 Educational/commercial Qt artifact가 섞임 | 중간 / 치명적 | 전체 product CI는 Qt Community artifact만 허용한다. 사용 module별 license self-audit와 clean Linux source rebuild를 거쳐 20-device 제한 alpha와 public APK를 열고, F-Droid·Play·Full Distribution을 각각 별도 gate로 둔다. 앱이 무료라는 사실을 근거로 의무를 생략하지 않는다. |
| P0 | Ugurugu GPL·Qt LGPL과 App Store 정적 링크·추가 조건의 양립이 확정되지 않음 | 높음 / 치명적 | Qt에 구체적인 서면 답변을 요청하고 무료 법률 clinic 또는 전문 자문으로 독립 검토한다. 승인 전에는 본인 기기 development build만 허용하고 외부 TestFlight/App Store를 차단한다. 상용 Qt도 앱 자체 GPL·기여자 권리를 자동 해결하지 않는다. |
| P0 | Qt의 stylus event가 Pencil coalesced/predicted/estimated 또는 Android palm-cancel/history를 손실할 수 있음 | 높음 / 높음 | Qt와 UIKit/MotionEvent raw trace를 동일 stroke에서 비교한다. 빠진 필드만 native batch adapter로 보완하고 trace fixture로 회귀시킨다. |
| P0 | 4K QImage, full JSON DOM, frame cache, all-frame export가 합쳐져 OOM/process kill | 높음 / 치명적 | peak allocation ledger를 먼저 만들고 stream serialization/export, tile/cache eviction, reservation, worker 제한을 구현한다. 저사양 실제 기기에서 1/4/60 frame corpus를 측정한다. |
| P0 | 현재 CMake의 `APPLE == macOS` 가정과 Sparkle/AppKit가 iOS configure를 막음 | 확실 / 높음 | `IOS`, `MACOS`, `ANDROID` target을 분리하고 app/update/package를 core에서 떼는 설계안을 2단계 exit criterion으로 둔다. |
| P1 | CPU QPainter renderer와 매 frame texture upload가 4K·120 Hz에서 병목 | 중~높음 / 높음 | dirty tile upload와 display-scale surface를 먼저 유지하고 profile한다. active stroke preview만 GPU/저해상도로 옮길지를 측정으로 결정하며 전면 GPU 재작성은 최후 수단으로 둔다. |
| P1 | QRhi/GuiPrivate의 Qt patch 호환성, render thread, backend/device loss | 중간 / 높음 | Qt patch를 pin하고 upgrade lane을 별도로 둔다. Metal/Vulkan/GLES golden·resize·suspend·device recreation test와 GLES fallback을 release gate로 둔다. |
| P1 | Android GPU/펜/RAM/provider fragmentation과 16KB native page size | 높음 / 높음 | Samsung·Pixel/일반·low-RAM 세 tier, Vulkan/GLES 두 backend, 16KB emulator/device에서 AAB의 모든 `.so`와 libwebp/spdlog를 검사한다. 지원 밖 기기는 명시한다. |
| P1 | background callback에서 512 MiB serialization이나 unbounded thread join을 시작해 OS가 process를 종료 | 높음 / 높음 | 편집 중 작은 journal과 idle checkpoint를 지속하고 lifecycle에는 짧은 flush만 한다. expiration/cancel, per-document generation, forced process-death test를 추가한다. |
| P1 | iCloud/SAF provider가 path·seek·atomic rename·항상-online을 보장하지 않음 | 높음 / 높음 | URI/URL identity와 permission을 repository가 소유한다. internal temp 완성 후 coordinated copy/replace하고 stale/read-only/offline/conflict를 상태로 모델링한다. |
| P1 | pen과 손가락 gesture가 동시에 document/viewport를 수정해 ghost stroke나 잘못된 undo 생성 | 중간 / 높음 | 하나의 arbitration state machine, pointer ownership, cancel rollback, predicted/actual 분리와 deterministic multi-pointer trace를 사용한다. |
| P2 | desktop Widgets와 mobile QML의 command 노출·동작이 갈라짐 | 중간 / 중간 | command/view model을 공유하고 capability manifest와 parity test를 유지한다. 화면 topology만 다르게 한다. |
| P2 | Qt runtime/QML/plugin으로 download size와 cold start가 native보다 커짐 | 높음 / 중간 | release thinned IPA/split APK 기준으로 plugin/import/asset 기여도를 측정하고 사용하지 않는 Qt module, QML import, ABI를 제외한다. 기능을 줄여 수치를 숨기지 않는다. |
| P2 | Apple/Google SDK·정책 기한이 개발 중 변경 | 높음 / 높음 | 분기별 및 RC 직전 공식 정책 audit, Xcode/SDK/Qt upgrade rehearsal, store preflight owner를 release checklist에 둔다. |

라이선스 gate의 산출물은 “앱이 무료다” 또는 “Qt를 산다” 같은 단일 문장이 아니라 배포 채널별 승인서여야 한다.

- **비용 0원의 기본 경로:** Ugurugu product의 local·CI·공개 build를 모두 Qt Community Edition으로 통일한다. Android APK와 함께 Ugurugu·사용한 Qt의 complete corresponding source, 정확한 build/re-sign/install 절차, license text와 notice를 제공한다. 최대 20대의 닫힌 Limited Distribution alpha를 먼저 검증하고, public release page의 APK에는 그 밖의 사용자가 advanced flow/ADB를 거칠 수 있음을 알린다. F-Droid는 inclusion 후보이며 저마찰 광범위 Full Distribution에는 2026년 정책상 일회성 US$25가 필요하다.
- **Educational License 경계:** Qt Educational License는 별도의 학습 sandbox에만 쓴다. Ugurugu의 local PoC에도 사용하지 않아 같은 product에서 Community와 commercial 계열 artifact가 섞일 위험을 없앤다. 무료 앱인지 여부로 이 제한이 바뀐다고 가정하지 않는다.
- **나중에 유료화할 경우:** GPL은 판매 자체를 금지하지 않으므로 전체 source와 사용자 권리를 계속 제공한다면 Qt Community/GPL 경로를 유지할 수 있다. 가격을 받는 순간 자동으로 상용 Qt가 필요한 것은 아니지만, source 비공개·재배포 제한·유료 proprietary 전환을 원하면 그 시점 전에 별도 license·기여자 권리 gate를 다시 연다.
- **iOS 공개 경로:** source·relink/install 정보, App Store의 추가 조건과 code signing을 포함한 준수안을 독립적으로 검토하고 Qt Company에 프로젝트 조건을 적은 서면 답변을 요청한다. 승인 전에는 본인 등록 기기의 development build만 만든다.
- 앱 저작권을 모두 통제할 수 있으면 Ugurugu에 store 조건을 허용하는 별도/이중 라이선스를 검토할 수 있지만 이는 Qt의 권리를 바꾸지 않는다. `README.md@09c7582:196-202`가 앱 아이콘의 저작권자를 `seuppi`로 명시하고 `CONTRIBUTING.md@09c7582:8-21`은 기여자의 저작권을 양도받지 않으므로, 아이콘의 별도 허락 또는 교체와 모든 외부 기여의 동의·교체가 필요하다. 향후 기여를 받기 전에 GPL-only를 유지할지 별도 contributor agreement를 도입할지 결정하며, Git author 목록만으로 권리 소유를 확정하지 않는다.
- iOS에서 Qt를 제거하는 경로를 택하면 표준 C++ document/render core + UIKit/Objective-C++ + Metal의 재작성 규모를 별도 산정한다. UIKit shell만 바꾸고 Qt Core/Gui/QImage/QPainter를 남기는 것은 de-Qt가 아니다.
- Community/open-source Qt로 시작한 뒤 상용으로 전환하는 경우 Qt 공식 FAQ가 요구하는 **Qt Company의 사전 서면 동의**를 받고 계약 가능성·적용 시점을 확인한다. 예산에 없는 상용 전환을 일정의 확정 가정으로 두지 않는다.

가장 비싼 실패는 “iPad UI를 완성한 뒤 공개할 수 없거나, Android OOM 또는 raw stylus 입력 누락을 발견하는 것”이다. 따라서 Android 무비용 release package, 양쪽 입력, 메모리를 UI feature 개발 전에 같은 사전 검증 단계에서 닫는다.

## 12. TestFlight, App Store, Google Play 배포 준비

예산 0원의 출시 단계는 **Android 20-device 닫힌 alpha → public developer-signed APK와 advanced-flow 고지 → F-Droid inclusion 후보 → 필요할 때 US$25 Full Distribution 또는 Google Play → 배포 권리 승인 뒤 TestFlight/App Store**다. 이는 한 계정의 자동 승격 순서가 아니라 별도 배포 gate다. 무료 공개는 GPL/LGPL 의무를 면제하지 않는다. release binary마다 같은 revision의 source와 build 자료를 함께 보존한다.

### 12.0 무비용 Android alpha와 설치 계보

1. **최초 alpha artifact**
   - release page에 developer-signed universal 또는 arm64 APK, 같은 tag의 source archive, build·install 안내, APK SHA-256과 signing certificate SHA-256 fingerprint를 함께 올린다. AAB는 사용자가 직접 설치하는 artifact가 아니라 Play용이므로 초기 직접 배포에는 APK를 사용한다.
   - 최초 production signing key를 source 밖에 암호화해 두 곳 이상 백업한다. key를 잃거나 채널마다 다른 key를 쓰면 기존 설치 위에 update할 수 없다.
   - Android Developer Console의 무료 Limited Distribution은 사용자가 명시적으로 승인한 최대 20대까지다. 확인일의 공식 문서는 2026년 8월 출시 예정이며 early access가 닫혀 있다고 표시하므로 milestone 시작 때 실제 이용 가능 여부를 다시 확인한다. 2026-09-30부터 일부 지역·참여 store에서 인증이 시작되고 2027년 이후 global rollout이 예정되어 있어, 직접 APK가 무제한 무료 공개를 영구 보장하지 않는다. 등록하지 않은 app은 사용자가 advanced flow 또는 ADB를 거쳐야 할 수 있다.
2. **F-Droid 후보**
   - public source repository, 자유 software dependency만 사용하는 clean Linux CLI build, version tag와 `fdroiddata` recipe가 inclusion review를 통과해야 한다. F-Droid는 upload 즉시 배포되는 저장소가 아니다.
   - 기본 F-Droid 서명 build는 upstream 직접 APK와 signing key가 달라 서로 update할 수 없다. 한 설치 계보를 유지하려면 upstream-signed reproducible build가 F-Droid에서 검증되는 방식을 목표로 하거나, 서로 다른 package/channel을 감수한다.
   - F-Droid도 Android Developer Verification을 자동 우회한다고 가정하지 않는다. 출시 지역·기기에서 F-Droid의 참여 상태와 unregistered-app advanced flow를 RC 때 다시 확인한다.
3. **향후 Full Distribution·Play 전환**
   - 20대를 넘는 저마찰 일반 공개에는 현재 Android Developer Console Full Distribution의 일회성 US$25 또는 Google Play 계정이 필요하다. 비용 0원을 유지하면 설치자가 advanced flow/ADB를 거치는 마찰을 감수한다.
   - Android Developer Console의 Limited와 Full plan은 계정 생성 뒤 바꿀 수 없다. Full로 옮길 때는 새 account를 만들고 package name을 transfer해야 하므로 signing key/update 계보와 별개로 account migration을 계획한다.
   - 직접 APK와 Play build의 update 계보를 유지하려면 처음부터 같은 application ID와 app signing key를 사용한다. Play App Signing 도입 시 여러 store에 쓰던 기존 app signing key를 제공하는 경로를 선택하고 upload key는 별도로 운용한다.

### 12.1 Apple: 인증서, 서명과 App Store Connect

Apple Developer Program 계정 가입 비용과 가입 절차는 범위에서 제외한다. 아래는 iOS 공개 배포 권리가 서면 승인된 뒤 수행할 조건부 절차다. 그전에는 보유 계정으로 본인 등록 기기에 development signing한 local PoC만 만들고, 타인 대상 TestFlight와 App Store upload는 하지 않는다.

1. **식별자와 capability**
   - 고유 bundle ID를 정하고 explicit App ID를 등록한다. App Store Connect app record의 bundle ID와 정확히 일치시킨다.
   - primary language·bundle ID·SKU로 App Store Connect app record를 조기에 만들고, distribution profile과 첫 build upload보다 먼저 식별자를 고정한다.
   - iCloud Documents/container를 사용할 때만 해당 capability와 container identifier를 등록한다. document types/UTType(`.ugu`), open-in-place 정책, supported orientations를 Info.plist와 실제 동작에 맞춘다.
   - 개발, staging, production 식별자와 iCloud container를 임의로 섞지 않는다.
2. **인증서와 Provisioning Profile**
   - 개발 기기를 등록하고 실제 기기용 Apple Development 인증서·development profile, 배포용 Apple Distribution 인증서·App Store Connect distribution profile을 준비한다. TestFlight/App Store build는 개발 기기 목록을 포함하지 않는다.
   - Xcode automatic signing을 로컬 개발에 사용할 수 있지만 CI/release에서 누가 profile을 갱신하는지, certificate 만료·폐기·복구 절차와 접근 권한을 명시한다. manual signing이면 explicit App ID, distribution certificate와 entitlement가 일치하는 profile을 생성한다.
   - private key와 App Store Connect API key는 source와 artifact에서 분리하고 최소 권한 secret store에 보관한다.
3. **빌드와 archive**
   - 2026-04-28 이후 제출 요구에 따라 Xcode 26 이상과 iOS/iPadOS 26 SDK로 archive한다. 최소 실행 버전 iOS/iPadOS 17은 별도 deployment target이다.
   - Qt iOS CMake가 만든 Xcode project를 먼저 Release로 build한 뒤 arm64 archive하고 Validate App을 통과시킨다. Qt 문서상 Apple Silicon의 iOS simulator library는 기본 x86_64/Rosetta 경로이므로, Xcode 26 iOS platform의 universal architecture component 설치와 simulator/device build를 사전 검증한다.
   - CMake Xcode archive에 dSYM이 빠지는 Qt 문서상의 제약을 검사한다. 필요하면 `QT_USE_RISKY_DSYM_ARCHIVING_WORKAROUND`를 **archive 전용 configure**에서만 시험하고, generator expression·runtime output directory 부작용을 검토한 뒤 채택한다. option 이름처럼 위험한 우회이므로 기본 개발 build에 켜지 않는다.
   - app icon, launch screen, localization, permission purpose string, export compliance, encryption 사용을 확정한다.
   - marketing version과 별도로 같은 version 내 각 upload의 `CFBundleVersion`을 고유하게 증가시킨다. 각 제출 archive와 UUID가 맞는 dSYM을 보존하고 App Store Connect/TestFlight crash가 symbolicate되는지 의도적 test crash로 확인한다.
4. **privacy와 SDK 공급망**
   - App Privacy 답변에는 앱과 Qt/libwebp/spdlog를 포함한 third-party code의 수집·추적을 모두 반영하고 공개 privacy policy URL을 제공한다.
   - `PrivacyInfo.xcprivacy`에 사용하는 required-reason API와 승인 사유를 선언한다. Apple 지정 third-party SDK의 privacy manifest·signature 요구와 새 SDK 추가를 release마다 검사한다.
5. **App Store Connect와 TestFlight**
   - 앞서 만든 app record의 version·metadata를 채운 뒤 CI archive를 업로드하고 processing·entitlement·symbol 경고를 확인한다.
   - 먼저 내부 tester(최대 100명), 그다음 외부 group으로 확장한다. TestFlight build는 90일 유효하고 외부 tester는 최대 10,000명이며 group의 첫 외부 build에는 Beta App Review가 필요하다.
   - iPad Pencil/Stage Manager, iPhone compact UI, 문서 provider별 cohort를 나누고 screenshot/comment/crash feedback를 triage한다.
6. **심사와 출시**
   - iPhone·iPad 실제 UI screenshot, 설명, keywords, support/privacy URL, age rating, app privacy, copyright를 제출한다.
   - reviewer가 Pencil 없이도 모든 필수 기능을 쓸 수 있게 하고, sample `.ugu`, import/export 절차, iCloud 사용 이유, 비명백한 gesture와 test instructions를 review note에 적는다.
   - crash/placeholder/깨진 link가 없는 production-signed build를 제출하고 rejection owner와 응답 SLA를 정한다. 승인 후 phased release, crash·save-failure guardrail과 rollback/새 build 절차를 사용한다.

### 12.2 Android: 무비용 공개, Play Console, AAB와 서명

첫 20-device 닫힌 alpha에는 Google Play가 필요 없지만 Android Developer Console의 Limited Distribution 등록과 device authorization을 준비한다. public APK를 무료로 제공할 수는 있어도 그 밖의 설치자에게 advanced flow/ADB 마찰이 생길 수 있다. 저마찰 광범위 공개에는 Full Distribution 또는 Play의 일회성 US$25를 예산에 넣는다. F-Droid inclusion도 비용은 없지만 별도 심사·build와 Android 인증 정책의 영향을 받는다.

Google Play Console 계정 보유 여부는 현재 정보에 없다. 나중에 Play가 필요해질 때 개인/조직 선택과 신원 검증을 계획한다. 2023-11-13 이후 생성된 **개인 계정**에는 production access 전에 최소 12명이 14일 연속 참여하는 closed test 요건이 적용될 수 있으므로 조직 계정 전체에 일반화하지 말고 실제 계정 상태를 확인한다.

1. **오픈소스 release package**
   - APK/AAB와 정확히 같은 Git tag의 Ugurugu complete corresponding source, submodule/dependency lock, CMake·Gradle·NDK·Qt version과 build command를 제공한다.
   - 사용한 Qt library의 complete corresponding source와 modification, 또는 수령 방법을 유효한 방식으로 제공하고 GPL/LGPL 전문과 prominent notice를 앱과 download page에 포함한다.
   - Qt module별 선택 license를 manifest에 기록한다. Ugurugu와 결합하는 Qt module은 GPLv3 option을 기본으로 하고, LGPL로 배포하는 component가 있으면 교체·relink·실행 절차를 별도로 시험한다.
   - 사용자가 source를 수정해 build·서명·설치하고 Qt library를 교체할 수 있는 절차를 실제 clean environment에서 검증한다. 단순 repository URL이나 “동적 링크했다”는 설명만으로 준수가 끝난다고 간주하지 않는다.
   - Qt Educational/commercial binary, proprietary Qt module, 재배포 불가능한 SDK나 asset가 release dependency graph에 섞이면 CI가 실패하게 한다.
2. **application ID와 빌드 규격**
   - 변경하기 어려운 package/application ID를 확정하고 2026-09-30 package 등록·developer identity 상태를 Android Developer Console 또는 Play Console에서 확인한다. 기존 Play 앱은 대부분 자동 등록되지만 확인은 필요하다.
   - 출시 계획은 `targetSdk 36`, Qt 6.11 지원 하한인 `minSdk 28`을 초기값으로 한다. 2026-08-31부터 신규 앱·업데이트에 API 36 target 요구가 적용되므로 RC에서 정책을 다시 확인한다.
   - Play용은 AAB로 만들고 최소 `arm64-v8a`를 포함한다. emulator/QA용 `x86_64`를 분리하거나 AAB에 함께 넣되 실제 download size를 확인한다. 모든 native `.so`가 64-bit와 16KB page size를 지원하는지 검사한다. Qt 6.10+ 지원만 믿지 말고 bundled libwebp/spdlog와 자체 binary까지 AAB Analyzer·16KB 환경에서 검증한다.
   - 사용자에게 보이는 `versionName`과 별도로 Play에 올리는 모든 artifact의 `versionCode`를 단조 증가시키고 CI가 중복·감소를 거부한다.
3. **서명**
   - 최초 직접 APK에 사용한 app signing key를 source 밖에서 생성·백업한다. 나중에 Play App Signing을 도입하면 여러 store의 update 계보를 유지할 수 있도록 기존 app signing key 제공 방식을 선택하고, 별도 upload key로 AAB를 업로드한다.
   - upload key와 app signing key의 역할, rotation/recovery 담당자를 문서화하고 debug/staging key로 production package를 만들지 못하게 CI에서 검사한다.
4. **manifest와 파일 접근**
   - intent filter에 `.ugu` MIME/extension과 incoming open/share를 선언하고 content URI만 사용한다. broad storage permission 대신 SAF/Photo Picker/app-specific storage를 사용한다.
   - recovery와 private metadata는 `filesDir`, 재생성 가능한 preview/cache는 `cacheDir`에 둔다. 사용자가 보존할 최종 문서는 uninstall 시 제거되는 app-specific storage가 아니라 SAF를 통해 선택한 provider에 저장한다.
   - edge-to-edge/window inset, configuration change, foreground service를 쓰지 않는 background save, network·notification permission 부재를 manifest와 Data Safety에 맞춘다.
5. **Play Console 정책 정보**
   - store listing, phone/tablet screenshot, feature graphic, category, support 연락처, privacy policy를 준비한다.
   - “수집 없음”인 경우에도 출시 track에는 Data Safety form과 privacy policy가 필요하다. third-party SDK의 data behavior까지 포함한다. IARC content rating, target audience/content, ads 여부, app access, 민감 permission declaration을 완료한다.
   - 2026 developer verification/package registration과 release 시점의 policy status를 다시 확인한다.
6. **테스트와 출시**
   - internal test(최대 100명) → closed test → 필요 시 open test 순서로 승격하고 Play pre-launch report와 실제 S Pen/low-RAM/foldable lab 결과를 함께 본다.
   - release AAB에 `FULL` 또는 적절한 native debug symbols를 포함하거나 별도 업로드해 C++/Qt crash를 symbolicate하고 version별 symbols를 보존한다.
   - staged rollout을 작은 비율에서 시작하고 crash/ANR, OOM/LMK, save/export failure, backend fallback 비율을 guardrail로 둔다. 문제가 생기면 rollout을 중지하고 새 signed version으로 복구한다.

### 12.3 Android와 조건부 Apple 공통 release gate

- 동일 source revision, dependency lock, compiler/SDK/Qt patch와 signing identity로 build가 재현된다.
- binary 수령자가 같은 version의 Ugurugu·Qt 대응 source, license text, build·relink·재서명·설치 정보에 접근할 수 있고 clean rebuild가 검증된다.
- 개인정보 전송이 없는 경우에도 검증 로그·crash telemetry의 데이터 흐름, retention, opt-out을 privacy 문서와 일치시킨다.
- 제출 binary의 SBOM/third-party notice, GPL/Qt 결론, libwebp/spdlog license와 vulnerability audit가 승인돼 있다.
- clean install, upgrade, rollback 불가 상황, offline first launch, corrupted document, storage full, process kill을 production-signed artifact로 검증한다.
- iOS dSYM/Xcode archive와 Android native symbols/upload mapping을 version별로 보존하고 실제 crash 한 건을 양쪽에서 symbolicate한다.

## 13. 개발 우선순위와 대략적인 작업 규모

### 13.1 우선순위

| 순위 | 작업 | 이유 |
|---:|---|---|
| **P0-1** | Qt Community Android 무비용 배포 package와 iOS 보류/승인 결정 | Educational License 오용을 막고 실제 공개 가능한 첫 target을 feature 개발 전에 고정한다. |
| **P0-2** | Android tablet primary + iPad local stylus/QRhi PoC | architecture의 native adapter 범위와 QML canvas 가능성을 결정한다. |
| **P0-3** | 4K serialize/render/export peak memory 및 lifecycle kill PoC | 가장 큰 기술 일정 변동 요인이며 코어 API를 바꿀 수 있다. |
| **P1-1** | path-free document/session façade와 mobile build graph | 파일 provider, autosave, UI를 독립시키는 모든 후속 작업의 경계다. |
| **P1-2** | shared QML tablet UI와 input arbitration | 제품 가치가 있는 첫 vertical slice다. |
| **P1-3** | SAF, journal recovery와 bounded export; 조건부 iCloud | 저장·복구 없는 drawing demo를 Android 공개 가능한 앱으로 만든다. |
| **P1-4** | low-RAM/backend/device matrix와 성능 budget | Android 출시 가능 범위와 지원 기기를 확정한다. |
| **P2-1** | compact phone topology와 foldable polish | core/view model이 안정된 뒤 적용하면 중복을 줄일 수 있다. |
| **P2-2** | Pencil squeeze, S Pen Air Actions, advanced export/UI parity | 핵심 workflow와 데이터 안전성보다 후순위다. |
| **P3** | full GPU renderer 또는 native UI 재평가 | profile이 현재 구조의 한계를 입증할 때만 별도 투자 결정을 한다. |

### 13.2 workstream별 규모와 소유권

| workstream | 주 소유 | 규모 | 주 산출물 |
|---|---|---:|---|
| 라이선스·release architecture | 개발자 + 필요 시 법률 자문 | 0.5~1.0 인월 + 외부 대기 | Community Android 준수안, 기여자 권리 audit, iOS 서면 결정 |
| CMake/core/session/CI | senior Qt/C++ | 3~5 인월 | mobile core targets, service boundary, deterministic CI |
| renderer·memory·scheduler | graphics C++ | 4~6 인월 | QQuickRhiItem, Metal/Vulkan/GLES, bounded 4K pipeline |
| input·gesture | C++ + iOS/Android | 3~4.5 인월 | Pencil/S Pen batch adapter와 arbitration |
| mobile QML UX | QML/product UI | 4~6 인월 | tablet·phone adaptive components와 accessibility |
| storage·lifecycle·share | platform + C++ | 3~4.5 인월 | iCloud/Files, SAF, journal recovery, import/export/share |
| QA·performance·stores | QA + platform/release | 4~6 인월 | device matrix, telemetry, beta, store approval |

workstream 합은 경계 작업이 겹치므로 단계 합과 단순히 더하지 않는다. 일정은 사전 검증 뒤 ±30% 범위로 재산정하고, Android 태블릿 배포 MVP는 **10~16 인월**, 네 폼팩터 출시는 **21~33 인월**을 기준선으로 둔다. 1인 0.5 FTE의 학생 개발이면 Android tablet MVP만 약 **20~32개월**에 해당하며 학업·기기 확보·외부 심사 대기는 별도다. 인월을 병렬화할 수 없으므로 phone과 iOS를 후속 release로 분리한다.

## 부록 A. 조사 근거

### A.1 저장소 근거 색인

아래는 주요 판단의 대표 근거다. 별도 표시가 없는 architecture·code 위치는 `dbd497c` 기준이고, license·기여자 권리 위치는 `09c7582` 기준이다. 본문의 더 구체적인 판단에는 해당 위치를 함께 표시했다.

| 판단 | 저장소 위치 |
|---|---|
| Qt/CMake와 macOS·Windows 전용 build | `CMakeLists.txt:1-13`, `cmake/UguruguDependencies.cmake:3-146`, `cmake/UguruguTargets.cmake:9-102`, `139-158`, `CMakePresets.json:20-249`, `.github/workflows/ci.yml:139-210` |
| desktop Widgets app/UI | `src/main.cpp:150-229`, `src/ui/MainWindow.hpp:36-130`, `src/ui/MainWindow.cpp:160-252`, `1286-1352` |
| canvas input/session 결합 | `src/ui/CanvasWidget.hpp:228-283`, `368-507`, `src/ui/CanvasWidget.cpp:52-57`, `169-175`, `src/ui/CanvasWidgetEvents.cpp:47-72`, `210-630` |
| CPU renderer와 QRhi display | `src/render/RenderEngine.hpp:16-25`, `135-140`, `src/render/RenderEngine.cpp:38-103`, `src/render/IncrementalStrokeRenderer.cpp:48-203`, `src/ui/CanvasFrameView.cpp:77-261` |
| 문서·history·limit | `src/document/Document.hpp:164-250`, `src/document/DocumentLimits.hpp:8-50`, `src/document/history/LogicalHistoryCommand.hpp:15-26`, `src/document/history/DocumentDelta.hpp:16-129` |
| serializer·export | `src/io/serializer/SerializerSchema.hpp:16-25`, `src/io/DocumentSerializer.cpp:562-648`, `701-724`, `src/io/ExportWorker.cpp:246-372` |
| memory·thread·recovery | `src/app/MemoryBudget.hpp:8-31`, `src/app/MemoryBudget.cpp:27-68`, `src/app/RecoveryWriter.cpp:24-63`, `178-238`, `src/app/RecoveryStore.cpp:186-196`, `src/io/ExportWorker.cpp:46-62`, `src/ui/LayerDock.cpp:1002-1009`, `src/ui/CanvasWidgetSelection.cpp:393-430`, `src/app/BackgroundWork.cpp:8-11` |
| license·기여자 권리 (`09c7582`) | `README.md:196-222`, `CONTRIBUTING.md:3-21`, `THIRD_PARTY_NOTICES.md:9-23` |

### A.2 Qt 공식 문서

모두 **2026-08-06**에 확인했다.

- 지원 OS·toolchain: [Qt 6.11 Supported Platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- iOS build·Xcode generator·deployment target·Objective-C++: [Qt for iOS](https://doc.qt.io/qt-6/ios.html), [iOS Platform Notes](https://doc.qt.io/qt-6/ios-platform-notes.html)
- desktop port의 QML 권고: [Porting to Android](https://doc.qt.io/qt-6/porting-to-android.html)
- Qt Android app와 native embedding 비교: [Qt for Android](https://doc.qt.io/qt-6/android.html), [Qt Quick for Android](https://doc.qt.io/qt-6/qtquick-for-android.html)
- QtThread/Android UI thread·activity/lifecycle: [How Qt for Android Works](https://doc.qt.io/qt-6/android-how-it-works.html)
- APK/AAB/AAR와 multi-ABI 배포: [Deploying an Application on Android](https://doc.qt.io/qt-6/deployment-android.html)
- Android OpenGL/driver 주의: [Android Platform Notes](https://doc.qt.io/qt-6/android-platform-notes.html)
- Qt 6.10의 Android 16KB page size 지원: [What's New in Qt 6.10](https://doc.qt.io/qt-6.11/whatsnew610.html)
- Qt 6.11의 iOS native save/security-scoped resource 지원: [What's New in Qt 6.11](https://doc.qt.io/qt-6/whatsnew611.html)
- pressure·tilt·rotation·eraser: [QTabletEvent](https://doc.qt.io/qt-6/qtabletevent.html)
- touch sequence/cancel/mouse 합성: [QTouchEvent](https://doc.qt.io/qt-6/qtouchevent.html), [QEventPoint](https://doc.qt.io/qt-6/qeventpoint.html)
- QML gesture/input: [PinchHandler](https://doc.qt.io/qt-6/qml-qtquick-pinchhandler.html), [PointerHandler](https://doc.qt.io/qt-6/qml-qtquick-pointhandler.html)
- safe area: [SafeArea](https://doc.qt.io/qt-6/qml-qtquick-safearea.html), [QWindow safeAreaMargins](https://doc.qt.io/qt-6/qwindow.html)
- QRhi item, thread·DPR·호환성 제한: [QQuickRhiItem](https://doc.qt.io/qt-6/qquickrhiitem.html)
- Metal/Vulkan/OpenGL ES backend: [Graphics in Qt](https://doc.qt.io/qt-6/topics-graphics.html), [QQuickWindow graphics API](https://doc.qt.io/qt-6/qquickwindow.html)
- LGPL 의무와 app store/static linking 주의: [Qt Open Source Licensing Obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- Community Edition의 GPL/LGPL 선택: [Qt Open Source Development](https://www.qt.io/development/download-open-source), [Qt Licensing](https://www.qt.io/development/qt-framework/qt-licensing)
- 학습용 범위와 public product build 분리: [Qt Educational License](https://www.qt.io/development/qt-educational-license), [Qt Educational License Terms](https://www.qt.io/terms-conditions/edu-2023-11)
- Qt 상용 라이선스 범위: [Qt Commercial Licensing FAQ](https://www.qt.io/faq/qt-commercial-licensing)

### A.3 Apple 공식 문서

모두 **2026-08-06**에 확인했다.

- 제출 SDK 기한: [Upcoming Requirements](https://developer.apple.com/news/upcoming-requirements/)
- App Review 원칙: [App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/)
- Pencil force·altitude·azimuth·estimated/coalesced 입력: [Handling Input from Apple Pencil](https://developer.apple.com/documentation/uikit/handling-input-from-apple-pencil)
- 고주파 실제 sample: [Getting High-Fidelity Input with Coalesced Touches](https://developer.apple.com/documentation/uikit/getting-high-fidelity-input-with-coalesced-touches)
- 임시 예측 sample: [Incorporating Predicted Touches into an App](https://developer.apple.com/documentation/uikit/incorporating-predicted-touches-into-an-app)
- double tap·squeeze: [Apple Pencil Interactions](https://developer.apple.com/documentation/uikit/apple-pencil-interactions)
- hover pose/거리: [Adopting Hover Support for Apple Pencil](https://developer.apple.com/documentation/uikit/adopting-hover-support-for-apple-pencil), [UIHoverGestureRecognizer](https://developer.apple.com/documentation/uikit/uihovergesturerecognizer)
- iPad 전용 호환성과 Pencil 세대별 기능: [Apple Pencil Compatibility](https://support.apple.com/en-us/108937), [Apple Pencil Feature Comparison](https://www.apple.com/apple-pencil/)
- mouse/trackpad pointer: [Pointer Interactions](https://developer.apple.com/documentation/uikit/pointer-interactions)
- iPad input·multitasking 원칙: [Designing for iPadOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-ipados)
- window/trait 변화와 safe area: [Adapting Your App When Traits Change](https://developer.apple.com/documentation/uikit/adapting-your-app-when-traits-change), [UIView safeAreaLayoutGuide](https://developer.apple.com/documentation/uikit/uiview/safearealayoutguide)
- foreground/background와 memory: [Managing Your App's Life Cycle](https://developer.apple.com/documentation/uikit/managing-your-app-s-life-cycle)
- document picker와 persistent directory access: [UIDocumentPickerViewController](https://developer.apple.com/documentation/uikit/uidocumentpickerviewcontroller), [Providing Access to Directories](https://developer.apple.com/documentation/uikit/providing-access-to-directories)
- Files/iCloud document browser와 coordinated document: [UIDocumentBrowserViewController](https://developer.apple.com/documentation/uikit/uidocumentbrowserviewcontroller), [UIDocument](https://developer.apple.com/documentation/uikit/uidocument)
- 사진 선택/import: [PHPickerViewController](https://developer.apple.com/documentation/photosui/phpickerviewcontroller)
- 시스템 공유: [UIActivityViewController](https://developer.apple.com/documentation/uikit/uiactivityviewcontroller)
- sandbox 파일 위치: [Using the File System Effectively](https://developer.apple.com/documentation/foundation/using-the-file-system-effectively)
- Metal: [Metal](https://developer.apple.com/metal/)
- App ID: [Register an App ID](https://developer.apple.com/help/account/identifiers/register-an-app-id/)
- 인증서: [Certificates Overview](https://developer.apple.com/help/account/create-certificates/certificates-overview)
- App Store provisioning: [Create an App Store Provisioning Profile](https://developer.apple.com/help/account/provisioning-profiles/create-an-app-store-provisioning-profile)
- App Store Connect record와 build upload: [Add a New App](https://developer.apple.com/help/app-store-connect/create-an-app-record/add-a-new-app/), [Upload Builds](https://developer.apple.com/help/app-store-connect/manage-builds/upload-builds/)
- iOS build 번호: [CFBundleVersion](https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundleversion)
- TestFlight 인원·유효 기간·beta review: [TestFlight Overview](https://developer.apple.com/help/app-store-connect/test-a-beta-version/testflight-overview), [View Tester Feedback](https://developer.apple.com/help/app-store-connect/test-a-beta-version/view-tester-feedback/)
- privacy label/policy: [Manage App Privacy](https://developer.apple.com/help/app-store-connect/manage-app-information/manage-app-privacy/)
- privacy manifest와 SDK 요구: [Privacy Manifest Files](https://developer.apple.com/documentation/bundleresources/privacy-manifest-files), [Required-Reason APIs](https://developer.apple.com/documentation/bundleresources/describing-use-of-required-reason-api), [Third-Party SDK Requirements](https://developer.apple.com/support/third-party-SDK-requirements/)
- archive/dSYM과 symbolication: [Build Debugging Information](https://developer.apple.com/documentation/xcode/building-your-app-to-include-debugging-information), [Add Identifiable Symbols to a Crash Report](https://developer.apple.com/documentation/xcode/adding-identifiable-symbol-names-to-a-crash-report)

### A.4 Android와 Google Play 공식 문서

모두 **2026-08-06**에 확인했다. Samsung 링크는 Samsung의 공식 개발자 문서다.

- pressure·orientation·tilt·hover·palm·latency·prediction: [Advanced Stylus Features](https://developer.android.com/develop/ui/views/touch-and-input/stylus-input/advanced-stylus-features)
- Android 버전별 palm cancellation: [Stylus Palm Rejection](https://developer.android.com/develop/adaptive-apps/cookbook/stylus-palm-rejection)
- raw pointer/history/axis API: [MotionEvent](https://developer.android.com/reference/android/view/MotionEvent)
- large-screen keyboard·mouse·stylus 호환: [Input Compatibility on Large Screens](https://developer.android.com/develop/ui/compose/touch-input/input-compatibility-on-large-screens)
- S Pen Air Actions의 선택적 성격: [S Pen Remote Overview](https://developer.samsung.com/galaxy-spen-remote/overview.html), [Air Actions](https://developer.samsung.com/galaxy-spen-remote/air-actions.html)
- 동적 window size class: [Use Window Size Classes](https://developer.android.com/develop/adaptive-apps/guides/use-window-size-classes)
- fold/hinge: [Make Your App Fold Aware](https://developer.android.com/develop/adaptive-apps/guides/foldables/make-your-app-fold-aware)
- multi-window: [Support Multi-Window Mode](https://developer.android.com/develop/adaptive-apps/guides/support-multi-window-mode)
- SAF와 persistable URI: [Access Documents and Other Files](https://developer.android.com/training/data-storage/shared/documents-files)
- 사진 선택/import: [Android Photo Picker](https://developer.android.com/training/data-storage/shared/photopicker)
- `filesDir`/`cacheDir`: [App-Specific Storage](https://developer.android.com/training/data-storage/app-specific)
- Sharesheet/content URI: [Send Simple Data to Other Apps](https://developer.android.com/training/sharing/send)
- Activity lifecycle: [Activity Lifecycle](https://developer.android.com/guide/components/activities/activity-lifecycle)
- saved state에 대용량 데이터를 두지 않는 원칙: [Saving UI States](https://developer.android.com/topic/libraries/architecture/views/saving-states-views)
- memory/low-RAM: [Memory Overview](https://developer.android.com/topic/performance/memory-overview), [Manage Your App's Memory](https://developer.android.com/topic/performance/memory)
- Vulkan runtime 지원 확인: [NDK Stable APIs](https://developer.android.com/ndk/guides/stable_apis)
- JNI 경계 최소화: [JNI Tips](https://developer.android.com/ndk/guides/jni-tips)
- 16KB page size 의무: [Support 16KB Page Sizes](https://developer.android.com/guide/practices/page-sizes)
- target API 36 일정: [Target API Level Requirements](https://support.google.com/googleplay/android-developer/answer/11926878?hl=en-GB)
- 2026 developer verification, 20-device 무료 Limited Distribution과 full account: [Android Developer Verification](https://developer.android.com/developer-verification), [Limited Distribution](https://developer.android.com/developer-verification/guides/limited-distribution), [Android Developer Console Account](https://support.google.com/android-developer-console/answer/16604405?hl=en), [Choose a Distribution](https://support.google.com/android-developer-console/answer/16640817?hl=en), [Register Package Names](https://support.google.com/googleplay/android-developer/answer/16984799?hl=en)
- 직접 APK publish와 signing key: [Prepare and Roll Out a Release](https://developer.android.com/studio/publish/), [Sign Your App](https://developer.android.com/studio/publish/app-signing)
- Play 계정 등록·신원 확인: [Get Started with Play Console](https://support.google.com/googleplay/android-developer/answer/6112435?hl=en)
- 신규 개인 계정 테스트 조건: [App Testing Requirements for New Personal Developer Accounts](https://support.google.com/googleplay/android-developer/answer/14151465?hl=en-EN)
- AAB 의무: [Use Android App Bundles](https://support.google.com/googleplay/android-developer/answer/9844679?hl=en)
- Android `versionName`/`versionCode`: [Version Your App](https://developer.android.com/studio/publish/versioning)
- Play App Signing/upload key: [Sign Your App](https://developer.android.com/studio/publish/app-signing), [Use Play App Signing](https://support.google.com/googleplay/android-developer/answer/9842756?hl=en)
- 64-bit native code: [64-Bit Requirement](https://developer.android.com/google/play/requirements/64-bit)
- internal/closed/open test: [Set Up an Open, Closed, or Internal Test](https://support.google.com/googleplay/android-developer/answer/9845334?hl=en)
- Data Safety와 privacy policy: [Complete the Data Safety Form](https://support.google.com/googleplay/android-developer/answer/10787469?hl=en), [User Data Policy](https://support.google.com/googleplay/android-developer/answer/10144311?hl=en)
- store app content declaration: [Prepare Your App for Review](https://support.google.com/googleplay/android-developer/answer/9859455?hl=en)
- content rating: [IARC Content Rating](https://support.google.com/googleplay/android-developer/answer/9898843?hl=en)
- pre-launch report: [Use Pre-Launch Reports](https://support.google.com/googleplay/android-developer/answer/9842757?hl=en)
- C/C++ symbol upload: [Include Native Debug Symbols](https://developer.android.com/build/include-native-symbols), [Deobfuscate or Symbolicate Crash Stack Traces](https://support.google.com/googleplay/android-developer/answer/9848633?hl=en)
- F-Droid source-build 공개 후보: [Inclusion Policy](https://f-droid.org/en/docs/Inclusion_Policy/), [Submitting to F-Droid](https://f-droid.org/en/docs/Submitting_to_F-Droid_Quick_Start_Guide/), [Reproducible Builds](https://f-droid.org/en/docs/Reproducible_Builds/)

### A.5 대안 프레임워크와 언어 경계의 공식 문서

모두 **2026-08-06**에 확인했다.

- Swift/C++ 상호 운용과 제약: [Swift C++ Interoperability](https://www.swift.org/documentation/cxx-interop/), [Current Status](https://www.swift.org/documentation/cxx-interop/status/)
- Flutter engine·Dart/FFI/platform channel: [Flutter Architectural Overview](https://docs.flutter.dev/resources/architectural-overview), [Measuring App Size](https://docs.flutter.dev/perf/app-size)
- React Native JSI/Fabric와 native module: [React Native New Architecture](https://reactnative.dev/architecture/landing-page), [Native Platform](https://reactnative.dev/docs/native-platform)

### A.6 GPL과 배포 판단 자료

모두 **2026-08-06**에 확인했다. FSF의 App Store 사례는 2010년 당시 조건에 대한 역사적 분석이며 2026년 Apple 약관의 법률 결론으로 직접 사용하지 않고, 독립 검토가 필요한 위험의 근거로만 사용했다.

- GPL은 가격이 아니라 사용자 자유와 배포 조건에 관한 license라는 설명: [GNU GPLv3](https://www.gnu.org/licenses/gpl.en.html), [GNU License FAQ](https://www.gnu.org/licenses/gpl-faq.en.html)
- App Store 추가 조건과 GPL 충돌의 역사적 사례: [FSF: More about the App Store GPL Enforcement](https://www.fsf.org/blogs/licensing/more-about-the-app-store-gpl-enforcement)

## 최종 요약

- **모바일 이식 가능 여부:** 가능하다. 문서·history·CPU renderer·codec은 강한 기반이지만 현재 Widgets UI, direct-path I/O, desktop memory/lifecycle 정책과 build graph는 모바일 출시 준비가 되어 있지 않다.
- **가장 추천하는 아키텍처와 기술 스택:** Qt Community Edition 6.11.x/C++23 공통 코어 + iOS·Android 공용 Qt Quick/QML 모바일 UI + QQuickRhiItem/QRhi 1차 표시 후보(Metal, Vulkan·OpenGL ES launch-time fallback)다. UIKit/Objective-C++과 Kotlin/Java/JNI는 Qt가 충분히 노출하지 않는 고충실도 입력·문서·공유·수명주기만 담당하는 얇은 adapter로 쓴다. Ugurugu product tree에는 Qt Educational License를 사용하지 않는다.
- **첫 번째 목표 플랫폼:** 첫 MVP 플랫폼은 Android 태블릿이다. 서비스가 열리면 최대 20-device 닫힌 Limited Distribution alpha로 검증하고, public APK에는 advanced-flow/ADB 설치 마찰을 고지하며 F-Droid를 후속 후보로 둔다. 저마찰 광범위 공개는 US$25 별도 gate다. iPad와 Apple Pencil은 Community Qt로 만든 본인 기기 local shadow PoC이며 외부 TestFlight/App Store는 권리 승인 전까지 보류한다.
- **예상되는 가장 큰 장애물:** GPL/Qt/App Store 배포 조건과 기여자 권리, Android의 20-device 무료 한도·광범위 배포 비용/설치 마찰, 실제 stylus 기기 확보, 4K/animation peak memory, Pencil/S Pen raw input·palm/cancel 품질과 Widgets/session/path 결합이다.
- **가장 먼저 해야 할 작업:** Qt Community module·기여자 권리를 audit하고, 장기 application ID/signing key를 정해 clean Linux에서 Android APK와 대응 source package를 재빌드한다. 동시에 S Pen·일반 stylus·저사양 Android tablet과 iPad/Pencil 접근을 확보해 입력, QQuickRhiItem backend와 4K memory를 측정하고 Qt에 iOS 공개 조건을 서면 문의한다.
