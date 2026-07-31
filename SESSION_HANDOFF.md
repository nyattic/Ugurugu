# WagleWaglePaint 0.7.0 작업 인수인계

작성 시각: 2026-07-31 20:25 KST

이 문서는 세션을 초기화한 뒤 현재 작업을 안전하게 이어가기 위한 기록이다.
작성 직전에 실행 중이던 모든 보조 작업을 중단했다. 이 문서 작성 외에는 더
이상 소스 코드를 수정하지 않았다.

## 가장 먼저 확인할 것

- 기준 커밋은 `7d52756 feat: modernize UI and prepare 0.6.2`이며 현재 브랜치는
  `main`이다.
- 이번 작업은 아직 커밋하지 않았다. 사용자 변경을 포함한 큰 작업 트리이므로
  reset, checkout, clean 등으로 되돌리면 안 된다.
- 0.7.0 작업 전체가 의도된 변경이다. `release-notes/0.6.2.*.md` 삭제와
  `release-notes/0.7.0.*.md` 추가도 의도된 것이다.
- 마지막 전체 테스트 통과 뒤 `DocumentController`와 `DocumentTests`가 다시
  수정되던 도중 세션을 중단했다. 따라서 현재 HEAD+working tree 상태는 반드시
  다시 빌드하고 테스트해야 한다.
- 2026-07-31 20:25 기준 `git diff --check`는 통과했다.

현재 상태를 먼저 확인한다.

```sh
git status --short
git diff --check
git diff --stat
```

## 사용자가 요청한 방향

- 임시 처방이 아니라 문서 모델, 렌더링, 실행 취소, 저장 수명주기까지 근본적으로
  해결한다.
- 클립 스튜디오에 가까운 캔버스 영역 크기 변경과 이미지 크기 변경을 제공한다.
- 선택 영역 아래 액션바에서 이동, 크기 변경, 회전, 좌우/상하 반전, 복제, 삭제,
  선택 해제를 제공한다.
- 선택 변환은 미리보기 상태에서 여러 동작을 누적하고 Apply 시 한 번만 문서와
  실행 취소 기록에 반영한다. Cancel/Esc는 기록 없이 원상복구한다.
- 0.6.2는 실제로 릴리즈하지 않았으므로 0.6.2 릴리즈 노트를 0.7.0에 통합한다.
- 릴리즈 노트에는 빌드/릴리즈 절차 강화 같은 내부 작업을 쓰지 않는다.
- 커밋을 요청받기 전에는 커밋하지 않는다. 나중에 커밋할 때 trailer를 넣지 않는다.

## 이미 구현된 주요 기능

### 캔버스와 이미지 연산 모델

- 프로젝트 버전은 `CMakeLists.txt`에서 0.7.0이다.
- 문서 스키마 v6에 순서 보존 framebuffer operation을 추가했다.
  - `StrokeMode::PixelSelection`
  - `StrokeMode::Reframe`
  - 레이어별 `initialCanvasSize`
- 캔버스 자르기/확장을 단순 좌표 수정이 아니라 순서가 보존되는 Reframe 연산으로
  기록한다. 잘랐다가 다시 확장해도 이미 잘린 픽셀이 되살아나지 않는다.
- 이미지 크기 변경은 그림과 기존 브러시 두께가 함께 스케일되도록 순서 보존
  Reframe 연산으로 구현했다.
- 스키마 1~5 읽기 호환, 스키마 5 frozen fill 호환, 크기/압축 해제/마스크 예산
  검증이 포함되어 있다.
- 레이어 0개 상태를 정식으로 지원한다. 마지막 레이어 삭제가 가능하고, 그리기
  도구는 레이어 추가 안내를 표시한다.

### 캔버스/이미지 크기 UI와 줌

- `CanvasSizeDialog`:
  - 절대/상대 크기
  - 3x3 기준점
  - X/Y 오프셋
  - 잘림/전체 이탈/확장 경고와 시각적 미리보기
- `ImageSizeDialog`:
  - 픽셀 크기와 백분율
  - 가로세로 비율 유지
  - 왜곡 경고와 시각적 미리보기
- 줌은 1~1600%, 로그 슬라이더, 실제 픽셀 100%, 창 맞춤, macOS pinch,
  `Ctrl/Cmd+1`을 지원한다.

### 부동 선택 변환과 액션바

- `FloatingTransformSession`이 원본 선택 마스크, 원본 경계, 대상 레이어/연산 ID,
  누적 `QTransform`을 보관한다.
- 이동/크기 변경/회전/반전은 문서를 즉시 수정하지 않고 하나의 변환에 누적된다.
- Apply는 `DocumentController::transformSelection(...)`을 한 번 호출해
  PixelSelection 연산 하나와 undo 항목 하나만 만든다.
- Cancel/Esc는 문서와 history를 수정하지 않는다.
- 부분적으로 캔버스 밖에 나가는 변환은 허용한다. 완전히 밖으로 나간 상태는
  미리볼 수 있지만 Apply는 실패하고 세션을 유지한다.
- 선택 영역 내부 드래그만으로 자동 이동하지 않는다. 액션바의 Move 모드를
  켠 경우에만 드래그 이동한다.
- 액션바에는 Apply(Enter), Cancel(Esc), 이동, 크기 변경, 회전, 좌우/상하 반전,
  복제, 삭제, 선택 해제가 있다.
- 실제 `QAction` Return/Esc 라우팅 테스트가 있다.

### 선택 변환 렌더링과 메모리

- 화면이 축소된 경우 native 4K framebuffer를 먼저 만들지 않고 display scale에서
  순서 보존 연산을 재생한다. 저장/내보내기는 `NativeExact` 경로를 유지한다.
- 대기 중 변환은 매 paint마다 임시 Document에 op를 append하지 않는다. 캐시된
  `LayerSplitFrame::layerBase` 복사에 PixelSelectionOp 하나만 replay하고 합성한다.
- 20,000 stroke 문서의 대기 중 변환 replay에서 기존 primitive 재생 횟수 0을
  테스트로 고정했다.
- 4096x1, 1x4096처럼 한 축만 축소되는 캔버스도 display replay를 사용한다.
- packed selection mask는 4K에서 약 2 MiB이다.
- 선택 마스크 변환은 ROI 기반으로 변경했다.
  - 4K smooth 최악 explicit image surface peak: 80 MiB 이하
  - 작은 64x64 선택 ROI: 17 MiB 미만
  - packed identity: 32 MiB 이하
  - 기존 raster 결과와 픽셀 단위 동등성 유지
  - oversized target, NaN/비가역/perspective transform 선제 거부
- 이 부분은 Debug MaskRegression 12/12, ASan+UBSan MaskRegression 12/12를
  독립적으로 통과했다.

### Serializer와 성능 기반

- `DocumentSerializer::PreparedDocument`가 정규화된 불변 문서와 정확한 compact
  JSON 크기 계획을 함께 보관한다.
- 메타데이터만 바뀌는 편집은 기존 immutable QVector backing과 직렬화 계획을
  재사용한다.
- 압축 payload LRU는 64 MiB, 프로젝트 hard limit는 32 MiB이다.
- 이전 측정에서 20,000 stroke 문서의 rename/scalar prepare가 serializer에서
  약 14~17 us, controller 왕복은 약 1 ms였다.
- 화면 축소 4K selection preview의 가장 큰 QImage는 256x256, 추정 working set은
  약 576 KiB였다.

### 문서와 릴리즈 노트

- 한국어/영어/일본어 README에 캔버스, 이미지 크기, 줌, 부동 선택 변환,
  레이어 0개, 도구 설정 영속화, 정보 탭 내용을 반영했다.
- `release-notes/0.7.0.{ko,en,ja}.md`에 기존 0.6.2의 사용자 가시 변경을 전부
  통합했다.
- 내부 릴리즈 절차 관련 문구는 넣지 않았다.
- LICENSE는 정식 GPL-3.0 원문을 유지한다. 프로젝트 저작권 표시는 README
  하단으로 옮겨져 있다. GPL 원문 자체의 FSF 저작권 문구는 삭제하면 안 된다.

## 중단 시점의 실행 취소 리팩터링 상태

기존 count-only QUndoStack 구현에서 다음 구조 문제를 확인했다.

- 서로 다른 4K 마스크가 있는 history 64개가 약 1 GiB를 보유할 수 있었다.
- 기존 macro preflight가 각 child의 전체 PreparedDocument를 동시에 잡아
  20,000 stroke x 64 작업에서 약 1.5 GiB까지 올라갈 수 있었다.
- clear/remove undo가 immutable backing을 안전하게 재사용하지 못하면 마스크
  메모리를 중복 보유할 수 있었다.
- scalar 값을 바꿨다가 원래 값으로 돌린 net-zero merge가 불필요한 command와
  modified 상태를 남길 수 있었다.
- 임의 `std::function` effect가 외부 QImage alias를 history에 붙잡을 수 있었다.

이를 해결하기 위해 다음 구조가 작업 트리에 들어가 있다.

- public `DocumentUndoStack` facade는 유지하되 내부는 custom logical command
  vector와 cursor로 교체한다.
- 기본 history count limit 64와 resident byte budget 256 MiB를 함께 적용한다.
- 예산 초과 시 현재 cursor에서 가장 먼 prefix/suffix부터 제거해 가장 가까운
  undo/redo를 보존한다.
- 단일 항목 하나가 256 MiB를 넘는 경우 그 최신 항목 하나는 soft-retain한다.
- persistent effect는 typed variant journal만 사용한다. 임의 callback/lambda를
  history에 저장하지 않는다.
- 선택 UI history도 packed mask를 사용하는 typed
  `pushSelectionStateCommand`/`selectionHistoryStateRequested`로 바꿨다.
- macro는 start state와 working state만 유지하고 마지막에 start-to-end delta
  하나로 저장한다. 중첩 macro와 실패 rollback을 지원한다.
- `ImmutableBackingLease`는 PreparedDocument가 발급한 capability로, history undo가
  삭제된 stroke/mask backing을 외부 alias 위험 없이 재사용하게 한다.
- scalar net-zero merge는 항목을 제거하고 이전 node/revision/clean 상태로
  복원한다.
- `DocumentUndoStack`에 `Q_OBJECT`를 추가해 Undo/Redo 동적 문자열의 번역
  컨텍스트를 고정했다.

중요: 이 리팩터링을 담당하던 작업이 20:24 KST에 중단되었다.
`DocumentController.cpp/.hpp`와 `DocumentTests.cpp`는 마지막 198개 전체 테스트
통과 시점 이후 다시 수정되었으므로 현재 상태는 미검증이다. 우선 컴파일 오류,
부분 작성 테스트, 임시 코드가 없는지 diff를 검토해야 한다.

다음 계약 테스트가 모두 존재하고 통과하는지 확인한다.

- 서로 다른 4K mask history 64개에서 256 MiB 예산 준수와 가장 가까운 undo 보존
- count limit과 byte budget의 prefix/suffix eviction
- redo tail 삭제와 clean index/revision 보존
- 단일 oversized entry soft-retain
- net-zero scalar merge가 command, revision, modified 상태를 모두 원복
- nested macro, invalid child, preflight failure가 document/effect/signal/history를
  부분 이동시키지 않음
- macro가 동시에 보유하는 PreparedDocument 수가 상수 개수로 제한됨
- clear/remove undo가 immutable point/image/byte backing을 복사 없이 안전하게 재사용
- external alias는 trusted backing lease를 만들 수 없음
- synchronous callback 중 reentrant undo/redo/push/new/load/save가 차단됨
- QAction을 통한 undo/redo도 preflight를 우회하지 못함

## 반드시 해결해야 하는 미완료 P1: 부동 변환 수명주기

독립 검토에서 아래 두 문제가 확인되었고, 수정 작업을 시작하기 직전에 세션을
중단했다. 현재 `MainWindow`/`CanvasWidget`에는 아직 해결 코드가 들어가지 않았다.

### 1. 저장/닫기/내보내기/자동복구와 화면 상태 불일치

부동 변환은 CanvasWidget 로컬 상태라 `DocumentController::isModified()`에
포함되지 않는다. 현재 상태에서 가능한 문제:

- clean 문서에서 선택을 이동/회전한 뒤 Cmd+S를 누르면 화면의 변환이 아니라
  원본 문서가 저장되고 clean 처리될 수 있다.
- 그 상태에서 닫기/새 문서/열기를 하면 저장 질문 없이 화면의 변환이 사라질 수
  있다.
- PNG/GIF 내보내기와 autosave/recovery도 화면이 아니라 base 문서를 사용할 수
  있다.

권장 계약:

- CanvasWidget에 현재 pending PixelSelectionOp 하나를 Document 복사본에 append해
  화면과 같은 persistence/export snapshot을 만드는 명시적 API를 둔다. 이 경로는
  매 paint가 아니라 export/autosave에서만 사용한다.
- PNG/GIF export와 autosave는 snapshot을 사용하되 live document/history를
  수정하지 않는다.
- pending dirty 상태도 MainWindow의 unsaved/window-modified/autosave-pending 판단에
  포함한다.
- 명시적 Save/Save As는 파일 경로가 확정된 `saveToFile` 시 pending transform을
  한 번 Apply한 다음 저장하고 clean 처리한다. Apply 실패 시 저장을 중단하고
  현재 세션과 기존 파일을 보존한다.
- Save As 파일 대화상자를 취소하면 pending transform을 그대로 유지한다.
- `maybeSave()`는 controller modified 또는 pending dirty 중 하나라도 참이면
  Save/Don't Save/Cancel 창을 띄운다.
- Don't Save는 pending을 적용하지 않고 닫기/새 문서/열기를 허용한다.
- pending autosave를 쓴 뒤 변환을 취소한 경우 stale recovery가 남는 정책도
  명시적으로 처리한다.

필수 테스트:

- clean document + pending transform에서 window modified와 close prompt 표시
- Save 결과가 preview와 동일하며 transform command 하나만 추가되고 clean이 됨
- Apply 실패 시 save 중단, session/history/file 보존
- export/autosave snapshot이 preview와 동일하고 live history/index는 불변
- Save As dialog 취소 시 pending 유지

### 2. pending 상태의 Undo/Redo가 기존 history까지 이동

현재 MainWindow의 Undo/Redo QAction은 `DocumentUndoStack`에 직접 연결되어 있다.
pending transform 중 Undo를 누르면 controller history가 먼저 이동하고,
`documentChanged` 슬롯이 pending transform도 취소한다. 사용자는 한 번의 Undo로
두 상태를 잃는다.

권장 계약:

- MainWindow의 사용자 Undo 라우팅은 pending transform이 있으면 먼저 그 세션만
  취소하고 history index는 움직이지 않는다.
- 기존 document history가 하나도 없어도 pending이 있으면 Undo 동작은 활성화한다.
- pending 중 Redo는 최소한 기존 history를 이동시키지 않도록 비활성화하거나
  명시적으로 가로챈다.
- stack의 동적 action text/enabled 갱신은 유지하되, UI action을 안전하게
  mirror/wrap해 직접 연결로 인한 우회를 없앤다.
- controller API를 직접 호출한 외부 mutation은 기존 boundary 정책에 따라 pending을
  취소할 수 있지만, 사용자의 QAction/단축키는 위 계약을 따라야 한다.

필수 테스트:

- pending 중 Undo: session만 취소, document/history index/modified 불변
- base history가 없어도 Undo shortcut/action으로 pending 취소 가능
- pending 중 Redo: session 또는 history를 뜻하지 않게 이동하지 않음
- Apply 후 Undo: 적용된 PixelSelectionOp 하나만 정상적으로 undo

## 추가 독립 검토

독립 UI 검토 작업은 위 P1 두 건을 보고한 뒤 중단되었다. 새 세션에서 최종 소스가
안정된 다음 캔버스/이미지 크기 변경, 0 layer, 1xN/4K, selection action bar,
undo 원자성에 대한 adversarial review를 한 번 더 수행한다.

검토 후보(P2, 아직 확정 결함 아님):

- 1픽셀 높이/너비 이미지에서 `Keep aspect ratio`가 정수 반올림 때문에 축소를
  지나치게 제한하는지 확인
- 이미지 크기 변경에 Smooth/Nearest 보간 선택을 노출할 필요가 있는지 판단
- 폭이 매우 좁은 창에서 selection action bar가 캔버스 밖으로 넘치는지 확인

## 번역과 문서 마무리

소스가 안정된 뒤 아래 순서로 번역을 다시 추출한다.

```sh
lupdate src -locations absolute \
  -ts i18n/wobblepaint_ko.ts i18n/wobblepaint_ja.ts
rg -n 'type="unfinished"' i18n/wobblepaint_ko.ts i18n/wobblepaint_ja.ts
lrelease i18n/wobblepaint_ko.ts i18n/wobblepaint_ja.ts
xmllint --noout i18n/wobblepaint_ko.ts i18n/wobblepaint_ja.ts
```

새로 생긴 동적 history 문자열을 반드시 번역한다.

- `Undo`
- `Redo`
- `Undo %1`
- `Redo %1`

그 밖의 새 lifecycle 안내 문구도 한국어/일본어에 `unfinished` 없이 반영한다.

릴리즈 노트 확인 사항:

- 0.6.2의 frame scrubber, live wobble preview, Pretendard JP, 색상 UI,
  timeline 정리, macOS title bar, 그림자, hover wobble, focus ring, export label,
  update notes 디자인 항목이 모두 0.7.0에 들어 있어야 한다.
- zero-layer 기능은 이미 0.6.1에서 사용자에게 공개된 내용이므로 0.7.0 릴리즈
  노트에 중복 추가하지 않는다.
- 내부 history/serializer/release pipeline 구현 세부사항은 쓰지 않고 사용자에게
  체감되는 대형 캔버스 성능과 메모리 개선만 적는다.

## 최종 검증 순서

### 1. Debug

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

마지막 안정 시점에는 전체 198개 테스트가 통과했다.

- Document 77
- RenderEngine 47
- GIF 9
- Mask 12
- Release Notes 7
- UI 46

하지만 그 뒤 controller/history 테스트가 수정되었으므로 수와 결과가 달라질 수
있다.

### 2. Release

```sh
cmake --preset macos-release
cmake --build --preset macos-release
ctest --preset macos-release
```

### 3. ASan + UBSan

기존 빌드 디렉터리는 `out/build/audit-sanitized`이며 다음 플래그로 구성되어 있다.

```text
-fsanitize=address,undefined -fno-omit-frame-pointer
```

```sh
cmake --build out/build/audit-sanitized
ASAN_OPTIONS=detect_leaks=0 \
  QT_QPA_PLATFORM=offscreen \
  out/build/audit-sanitized/wobblepaint_tests
```

### 4. 릴리즈 노트 HTML

```sh
out/build/macos-release/wobblepaint_render_release_notes \
  release-notes/0.7.0.en.md /private/tmp/WagleWaglePaint-0.7.0.en.html
out/build/macos-release/wobblepaint_render_release_notes \
  release-notes/0.7.0.ko.md /private/tmp/WagleWaglePaint-0.7.0.ko.html
out/build/macos-release/wobblepaint_render_release_notes \
  release-notes/0.7.0.ja.md /private/tmp/WagleWaglePaint-0.7.0.ja.html
xmllint --html --noout /private/tmp/WagleWaglePaint-0.7.0.*.html
```

일본어 항목 사이의 간격과 다크 모드용 HTML/CSS도 확인한다.

### 5. macOS 설치/패키지 smoke

- 임시 install prefix에 `cmake --install`한다.
- `wobblepaint_package_smoke`/`tests/PreparePackageSmoke.cmake`와 `codesign --verify`
  를 실행한다.
- DMG를 만든 뒤 `hdiutil verify`한다.
- 앱 `Info.plist`에서 다음을 확인한다.
  - version 0.7.0
  - minimum macOS 14.0
  - Sparkle `SUFeedURL`
  - `SUPublicEDKey`

### 6. 최종 정적 확인

```sh
git diff --check
git status --short
rg -n '0\.6\.2|type="unfinished"' \
  CMakeLists.txt README*.md release-notes i18n src tests
```

0.6.2 문자열은 과거 호환 테스트나 의도된 설명이 아니라면 남아 있지 않아야 한다.

## 현재 변경 파일 개요

주요 수정 파일:

- `CMakeLists.txt`
- `README.md`, `README.en.md`, `README.ja.md`
- `i18n/wobblepaint_ko.ts`, `i18n/wobblepaint_ja.ts`
- `src/document/Document.*`
- `src/document/DocumentController.*`
- `src/document/DocumentLimits.hpp`
- `src/document/StrokeMask.*`
- `src/io/DocumentSerializer.*`
- `src/render/RenderEngine.*`
- `src/ui/CanvasWidget.*`, `MainWindow.*`, `Icons.*`
- `tests/DocumentTests.cpp`, `MaskRegressionTests.cpp`,
  `RenderEngineTests.cpp`, `UiTests.cpp`

새 파일:

- `release-notes/0.7.0.{ko,en,ja}.md`
- `src/document/SelectionOperation.{hpp,cpp}`
- `src/ui/CanvasSizeDialog.{hpp,cpp}`
- `src/ui/ImageSizeDialog.{hpp,cpp}`
- `src/ui/SelectionActionBar.{hpp,cpp}`

삭제 예정 파일:

- `release-notes/0.6.2.{ko,en,ja}.md`

## 커밋 전 체크

- 사용자가 명시적으로 커밋을 요청하기 전에는 커밋하지 않는다.
- 커밋 요청 시 먼저 전체 diff와 테스트 결과를 다시 확인한다.
- commit message trailer를 넣지 않는다.
- GitHub는 현재 sandbox 권한상 보이지 않을 수 있으므로 로컬 검증을 기준으로
  진행하고, 원격 작업은 사용자가 요청할 때만 한다.
