# 레이어 병합 — 지우개 적용 범위 고정 계획 (안 C)

> **구현 완료 (2026-08-05).** 전 단계가 끝났고 13개 suite 전부 통과한다.
> 구현 과정에서 §3의 설계를 한 가지 보정했다: 마커 **1개 삽입 + 전 구획
> 독립 렌더**로는 두 계약 테스트를 동시에 만족할 수 없다(병합 전 지우개와
> 병합 후 새 지우개가 stroke 목록상 같은 구획에 놓여 구별 불가). 실제
> 구현은 병합 시 source stroke를 **마커 쌍으로 감싸고**, 렌더러는 마지막
> 마커 이전의 구획들만 각자 표면에 봉인하며 **마지막 마커 이후(꼬리)는
> 평탄화된 합성 위에 직접 렌더**한다. 새로 그은 stroke는 꼬리에 붙으므로
> 새 지우개가 합쳐진 내용 전체에 닿는다. 이 의미론이 표현할 수 없는 유일한
> 병합 — 꼬리 지우개가 이미 합쳐진 그림을 지운 레이어를 다시 병합 —
> 은 계속 `UnsupportedStrokes`로 거부하며, 새 계약 테스트
> `refusesToMergeWhenATailEraserAlreadyReachedMergedArtwork`가 고정한다.

- 작성일: 2026-08-05
- 기준 커밋: `0dac1e4` (main)
- 작업 트리 상태: `tests/LayerCommandTests.cpp` **미커밋 변경 있음** (1단계 산출물, 의도적으로 실패하는 계약 테스트 2개)
- 목표: Clip Studio Paint처럼 "병합 버튼이 항상 눌리는" 동작을, 우글거림과 기존 문서의 외형을 잃지 않고 구현한다

## 1. 문제

레이어 1(위)에 선을 긋고 일부를 지운 뒤, 레이어 2(아래)의 같은 위치에 선이 있으면 병합이 거부된다.

`DocumentControllerLayers.cpp:486`

```cpp
if (!destructiveStrokesClearOf(current, *source, paintedContentBounds(current, target)))
    return MergeLayerDownStatus::UnsupportedStrokes;
```

원인은 `mergeLayerDown`이 stroke 목록을 이어붙이기 때문이다 (`DocumentControllerLayers.cpp:518`).

```cpp
target.strokes.append(source.strokes);
```

지우개는 **자기 레이어 안에서만** 앞선 픽셀을 지운다. 이어붙이면 아래 레이어의 선보다 뒤에 오게 되어 그 선까지 지운다. 가드는 이 손실을 막는 의도된 장치다.

### 실측 근거 (2026-08-05, macOS Debug)

| 시나리오 | `mergeLayerDownStatus` |
|---|---|
| 두 레이어의 페인트 선이 같은 위치에 겹침, 지우개 없음 | `0` Available |
| 위 레이어에 지우개, 아래 선과 같은 위치 | `4` UnsupportedStrokes |
| 위 레이어의 지우개가 아래 선에서 35px 떨어짐 (픽셀 겹침 없음) | `4` UnsupportedStrokes |

- 강제로 병합해 렌더 비교 시 **168픽셀 변화**, 지운 자리 중심이 `(0,0,0)` → `(1,1,1)`. 가드가 막는 손실은 실재한다.
- 세 번째 줄은 **오탐**이다. 판정이 경계 사각형끼리의 교차이고, 여백이 크다 (`StrokeCoverageRenderer.cpp:307`).

```
margin = displacement + max(0.5, width) × 1.025 × 2.1 + 4.0
```

`brushReach` 항은 우글거림이 0이어도 항상 더해진다. 굵기 8 선 → `QRect(0,28 100x44)`, 굵기 12 지우개 → `QRect(15,20 70x60)` (100×100 캔버스).

### `MergeLayerDownStatus` 열거값

`0` Available / `1` MissingLayer / `2` NoPaintLayerBelow / `3` UnsupportedProperties / `4` UnsupportedStrokes / `5` IncompatibleCanvasEpoch / `6` StrokeLimit

## 2. 대안 비교와 선택

CSP도 **벡터 레이어끼리 병합하면 같은 문제를 겪는다**. 투명 벡터선(지우개)이 아래 레이어 선을 먹으며, CSP 공식 권장 해법은 "래스터화한 뒤 병합"이다. CSP의 "항상 된다"는 체감은 래스터화의 결과이지 더 똑똑한 병합 알고리즘이 아니다. Ugurugu는 픽셀을 저장하지 않고 stroke를 프레임마다 다시 렌더하므로 CSP의 벡터 레이어 쪽에 서 있다.

| | 방식 | 병합 항상 가능 | 우글거림 | 기존 파일 외형 | 판정 |
|---|---|---|---|---|---|
| A | 두 레이어를 이미지로 구워 합침 | ✅ | ❌ 정지 | 유지 | 기각 |
| B | 지우개를 선 기하에 확정 | ✅ | ✅ | ❌ 변함 | 기각 |
| **C** | **지우개 적용 범위를 병합 시점에 고정** | ✅ | ✅ | ✅ | **채택** |
| D | 위 레이어만 구워서 붙임 | ✅ | 위만 정지 | 유지 | 보류 |

- **A 기각**: `StrokeMode::Image`는 프레임 인자를 받지 않는다 (`RenderEngineStrokes.cpp:240`, `applyImageOperation`). 구우면 그 부분의 우글거림이 사라진다 — 앱의 핵심 기능 손실. 프레임별로 굽는 변형은 기본 30프레임 × 캔버스 RGBA로 프로젝트 예산(128 MiB)과 스키마 양쪽에 부담.
- **B 기각**: 지우개와 선이 프레임마다 각각 따로 우글거리므로 정지 기하로 현재 화면을 재현할 수 없다. 구멍이 선을 따라다니게 되어 2.1.0이 약속한 "이전 버전 파일의 모습 유지"와 충돌.
- **D 보류**: 저렴하지만 위 레이어가 정지 이미지가 된다. C로 가는 길을 막지 않으므로, C가 지연되면 중간 단계로 쓸 수 있다.

## 3. 채택안 C의 설계

병합 시 stroke 목록에 **합성 경계 표식**을 삽입한다.

```
[아래 레이어 stroke들] → 경계 → [위 레이어 stroke들]
      구획 0 렌더                    구획 1 렌더
              └── 순서대로 source-over ──┘
```

- 레이어 내부를 경계로 구획한다. 각 구획을 투명 표면에 독립적으로 렌더한 뒤 순서대로 겹친다.
- 지우개는 **자기 구획 안에서만** 작용한다.
- 병합 가드가 이미 양쪽 레이어에 Normal 블렌드 + 불투명도 1.0을 요구하므로 단순 source-over로 **병합 전 두 레이어 합성과 완전히 동일**하다. 외형 보존이 구조적으로 보장된다.
- 병합 후 새로 그은 지우개는 최신 구획에 속하므로 합쳐진 내용까지 정상적으로 지운다.

### 표현 방식: 새 `StrokeMode` (필드 추가가 아님)

`Stroke`에 필드를 더하는 대신 새 `StrokeMode` 값(합성 경계 마커)으로 한다.

- `Reframe`이 이미 같은 성격의 중간 마커이고, `renderLayerOperations`가 마커에서 `flush()`하는 구조를 그대로 재사용할 수 있다.
- 마커가 없는 옛 파일은 자동으로 단일 구획 → 동작 불변. 마이그레이션 코드가 필요 없다.
- 마커 stroke는 `points` 비어 있음, 각 `*Op` 없음으로 검증한다 (`Reframe`/`PixelSelection` 검증 패턴과 동일).

### 메모리

늘지 않는다. 병합 전 레이어 표면 2장이 병합 후 구획 표면 2장이 될 뿐이다. 단 `LayerCompositionPlan::memoryEstimate`가 이를 반영해야 프리뷰 예산 판정이 맞는다.

## 4. 진행 상황

### 1단계 — 계약 테스트 (완료, 미커밋)

`tests/LayerCommandTests.cpp`에 2개 추가. 현재 코드에서 의도한 지점에서 결정적으로 실패한다.

| 테스트 | 내용 | 현재 결과 |
|---|---|---|
| `mergesALayerWhoseEraserOverlapsTheArtworkBelow` | 문제 시나리오 재현 → 병합 허용 + **애니메이션 전 프레임(30장) 픽셀 동일** + undo 복원 | status `4`, 기대 `0` |
| `letsANewEraserOnAMergedLayerReachWhatItWasMergedWith` | 병합 **후** 새 지우개는 합쳐진 내용까지 지움 | status `4`, 기대 `0` |

첫 번째가 핵심이다. 우글거림을 켠 채 전 프레임을 비교하므로 구획 합성이 정말 등가인지 프레임마다 검증한다. 두 번째는 구획 고정이 레이어를 영구히 갈라놓지 않음을 고정한다.

실행:

```
cmake --build --preset macos-debug --target ugurugu_tests
cd out/build/macos-debug && QT_QPA_PLATFORM=offscreen UGURUGU_TEST_SUITE=document \
  ./ugurugu_tests mergesALayerWhoseEraserOverlapsTheArtworkBelow \
                  letsANewEraserOnAMergedLayerReachWhatItWasMergedWith
```

`document` suite의 나머지 164개는 통과한다.

### 2단계 — 문서 모델과 직렬화

- `src/document/Document.hpp` — `StrokeMode`에 경계 값 추가
- `src/io/serializer/DocumentJsonCodec.cpp` (`StrokeMode::` 22곳) — 인코딩/디코딩
- `src/io/serializer/DocumentValidation.cpp` (8곳) — 마커 stroke 불변식
- `src/io/serializer/PreparedPlanBuilder.cpp` (3곳)
- 스키마 12 → 13. `tests/LayerCommandTests.cpp::roundTripsLayerHierarchySchema`가 `12`를 단언하고 있으므로 함께 갱신
- `src/io/SelectionClipboardCodec.cpp`, `src/io/WawaV10Importer.cpp` (9곳) 확인

이 단계까지는 렌더러가 마커를 모르므로 테스트는 계속 실패한다(실패 이유만 좁아진다).

### 3단계 — 렌더 구획 합성

- `src/render/engine/LayerOperationReplay.cpp:417` `renderLayerOperations` — 마커에서 `flush()` 후 현재 표면을 보관하고 새 투명 표면으로 전환, 마지막에 순서대로 source-over
- `src/render/engine/DisplayScaleReplay.cpp:239` `renderLayerOperationsAtDisplayScale` — 동일 처리
- **`Reframe`과의 상호작용 필수 처리**: 캔버스 크기 변경은 레이어 전체에 걸린다. 병합 후 리사이즈하면 Reframe stroke가 경계 **뒤에** 붙으므로, 보관 중인 **모든 구획에** 리프레임을 적용해야 한다. `applyReframeOperation`이 `QImage&`를 받으므로 구획마다 돌리면 된다.
- `PixelSelection`은 반대로 **현재 구획에만** 적용한다. 병합 전 위 레이어의 픽셀 이동이 아래 레이어를 건드리지 않았던 것과 같다.
- `Image` stroke는 현재 구획에 그린다.

### 4단계 — 커버리지 플래너와 프리뷰 (가장 무거움)

- `src/render/StrokeCoverageRenderer.cpp` (`StrokeMode::` 21곳) — erase 효과가 경계를 넘지 않도록. 이미 reframe 기준으로 레이어 내부를 epoch로 분할하는 구조(`StrokeCoveragePlan::epochs`, `epochBefore`)가 있어 확장 지점이 명확하다
- `src/render/IncrementalStrokeRenderer.cpp` (3곳) — 라이브 stroke 프리뷰
- `src/render/LayerCompositionPlan.cpp` — 구획 표면을 메모리 추정에 반영
- `src/render/RenderEngineStrokes.cpp` (16곳) — `renderStrokeCoverage`의 epoch 계산

### 5단계 — 병합 명령과 UI

- `DocumentControllerLayers.cpp:518` — `target.strokes.append(source.strokes)` 앞에 경계 마커 삽입
- `mergeLayerDownStatus` — `destructiveStrokesClearOf`에서 `Erase`/`PixelSelection` 검사 제거. **`Reframe`은 계속 거부**(레이어 전체에 걸리므로)
- stroke 수 한도 검사에 마커 1개 추가분 반영
- `src/ui/LayerDock.cpp` — `UnsupportedStrokes` 툴팁 문구 재검토
- `paintedContentBounds`가 쓰이지 않게 되면 제거

### 6단계 — 릴리즈 노트

`release-notes/2.1.0.{ko,en,ja}.md`의 "수정 / Fixed / 修正" 항목을 다시 쓴다. 현재 문구는 **거부** 동작을 설명하고 있어 사실과 달라진다.

> 레이어를 아래와 합칠 때 지우개로 지운 자국이 아래 그림까지 파고들던 문제를 고쳤습니다. 이런 경우에는 합치지 않고 이유를 알려 줍니다.

기술 용어를 피하고 기존 "증상 → 현재 동작" 서술을 따를 것.

## 5. 뒤집히는 기존 계약

| 대상 | 조치 |
|---|---|
| `tests/LayerCommandTests.cpp:785` `refusesToMergeALayerWhoseEraserWouldEatTheLayerBelow` | 현재의 거부를 고정하고 있다. 5단계에서 교체 |
| `release-notes/2.1.0.*` "수정" 항목 | 6단계에서 재작성 |
| `src/ui/LayerDock.cpp` `UnsupportedStrokes` 툴팁 | Reframe 전용 문구로 변경 |

## 6. 검증 절차

각 단계마다:

```
cmake --build --preset macos-debug --target ugurugu_tests
ctest --preset macos-debug                                   # 13 suite
cmake --build --preset macos-sanitized --target ugurugu_tests
ctest --preset macos-sanitized                               # ASan + UBSan
/Applications/Xcode.app/.../clang-format --dry-run --Werror <변경 파일>
/opt/homebrew/opt/llvm/bin/clang-tidy -p out/build/macos-debug \
  -extra-arg=-isysroot "-extra-arg=$(xcrun --sdk macosx --show-sdk-path)" <변경 소스>
```

번역 완결성(CI `Translations` job과 동일):

```
/opt/homebrew/opt/qt/bin/lupdate src -extensions cpp,hpp,mm -no-obsolete \
  -warnings-are-errors -ts <복사한 ko.ts> <복사한 ja.ts>
grep -c 'type="unfinished"' <복사본>    # 0이어야 함
```

3~4단계는 렌더 결과를 바꿀 위험이 크므로 `render`, `mask`, `ui_viewport` suite를 특히 볼 것. 픽셀 비교 테스트가 이미 많다 (`LayerCompositionTests`, `MaskRegressionTests`, `LegacyRenderGoldenTests`).

## 7. 부수 효과

C가 끝나면 `destructiveStrokesClearOf`의 경계 사각형 판정이 `Reframe` 검사만 남으므로, §1에서 확인한 **오탐(25px 떨어진 지우개도 차단)이 함께 사라진다.** D만 채택했다면 이 오탐 완화를 따로 해야 했다.
