# WWP 중심 Wiggle 기능 확장 계획

## 1. 목표

WiggleWiggleTool(이하 WWT)에서 유용했던 기능과 작업 흐름을 선별해 WagleWaglePaint(이하 WWP)에 맞게 다시 설계하고 구현한다.

> 편집 동작을 Clip Studio 관례로 정렬하는 `EDIT_BEHAVIOR_ALIGNMENT_PLAN.md`의 단계 A와 B는 완료됐다. 보류된 변형 핸들은 별도 작업이므로 이 계획을 더 이상 막지 않는다.

WWT는 비개발자가 AI를 활용해 만든 실험적 구현이므로 호환 대상이나 정답 구현으로 취급하지 않는다. WWT의 소스와 동작은 기능 후보를 파악하기 위한 참고자료로만 사용한다. 제품 동작, 데이터 모델, 렌더링 품질, 파일 형식, UI는 모두 WWP를 기준으로 결정한다.

확장이 끝난 시점의 목표는 다음과 같다.

- WWT에서 유용했던 선 표현, 채색, 선택, 이미지 삽입 흐름을 WWP다운 방식으로 사용할 수 있다.
- **기존 `.wagle` 파일의 렌더 결과가 v1.0.0과 픽셀 단위로 동일하다.**
- 새 기능도 WWP의 결정론적 렌더링, delta 기반 undo, 메모리 예산, 복구 체계를 따른다.
- WWT의 구현상 제약이나 버그를 복제하지 않는다.
- 선택적으로 제공하는 `.wawa` 가져오기는 호환 기능이 아니라 일회성 마이그레이션 도구로 취급한다.

### 우선 범위

- 시간축을 포함한 선 흔들림 표현 확장
- 허용치 기반 채색과 선택 개선
- 이미지 삽입을 위한 RGBA 자산과 이미지 연산
- Lasso Paint와 안전한 범위의 Merge Down
- 팔레트와 WWP 자체 프리셋
- 애니메이션 WebP 내보내기

### 조건부 범위

- Windows native `.wawa` 버전 10 읽기
- WWT `palette.txt`와 native `.wwpreset`의 일부 설정 가져오기
- 테마색과 앱 내 도움말

`.wwpreset`도 `.wawa`와 마찬가지로 같은 확장자에 포맷이 둘이다. native는 `WIGGLEWIGGLETOOL_PRESET=1`로 시작하는 key=value 텍스트고 웹판은 JSON이다. 구현한다면 결정 G와 같은 방식으로 native만 받고 웹판은 명확한 오류를 낸다.

조건부 기능은 핵심 기능을 지연시키지 않으며 각 단계에서 별도 go/no-go 결정을 내린다.

### 제외 범위

- WWT 렌더 결과의 픽셀 단위 재현
- WWT의 sin/cos 렌더 알고리즘과 GDI+ 래스터화의 1:1 포팅
- `.wawa` 쓰기와 완전한 버전 1~10 호환
- 웹판 JSON `.wawa` 지원
- WWT GameMaker 프로토타입과 WinForms UI 재현
- Windows 단축키를 포함한 `.wwpreset` 완전 호환
- `.ini` 런타임 번역 패치
- WAV 반복 재생

## 2. 확장 원칙

### 렌더는 WWP가 명세이고, 편집 관례는 아니다

기능을 옮길 때 우선순위는 다음과 같다.

1. 기존 `.wagle` 파일과 렌더 결과 보존
2. WWP의 결정론적·상태 없는 렌더링 유지
3. 문서 모델, undo, 직렬화, 메모리 예산과의 일관성
4. macOS와 Windows에서 같은 기능 계약 제공
5. WWT 기능의 의도와 작업 흐름 참고

WWT 코드와 결과가 위 원칙에 맞지 않으면 WWT 쪽 동작을 버리고 WWP에 맞게 다시 정의한다.

다만 **편집 동작의 관례는 WWP 현재 구현을 기준으로 삼지 않는다.** WWP 1.0.0에는 일반적인 그림 도구와 다르게 동작하는 지점이 있고(예: 선택 영역 복제가 같은 레이어에 쌓인다), 그런 곳은 Clip Studio 관례로 정렬한다. 정렬 대상과 판정은 `EDIT_BEHAVIOR_ALIGNMENT_PLAN.md`가 관리한다.

이 구분은 느슨하게 두지 않는다.

- **렌더 결과와 파일 형식**: WWP가 명세다. 기존 문서는 v1.0.0과 픽셀 단위로 같아야 한다
- **사용자가 조작했을 때의 동작**: 관례가 명세다. WWP가 관례에서 벗어나 있으면 WWP를 고친다

편집 동작을 바꾼다는 것이 렌더 경로를 바꿔도 된다는 뜻이 아니다. 새 기능이 기존 렌더를 건드리면 그건 여전히 회귀다.

### 코드는 독립적으로 구현한다

WWT는 C# / WinForms / GDI+, WWP는 C++23 / Qt 6 / QPainter다. 소스 병합은 하지 않는다. 기능의 사용자 가치와 입력·출력만 참고하고 WWP의 모듈 경계와 타입을 사용해 독립적으로 구현한다.

WWT 소스, 아이콘, 문구 또는 제3자 바이너리를 직접 반입해야 하는 기능은 권리와 라이선스를 먼저 확인한다. 단순 기능 아이디어만 참고하는 경우에도 구현은 WWP 코드 스타일과 테스트 기준으로 새로 작성한다.

### WWT 비교는 합격 기준이 아니다

WWT 화면과의 시각 비교는 기능 탐색과 UX 참고에는 사용할 수 있지만 자동 테스트의 oracle로 사용하지 않는다. 새 기능의 합격 기준은 WWP 내부의 결정론, 회귀 보존, 사용성, 성능과 안전성이다.

## 3. 현재 코드 기준 판단

WWP는 다음 기반을 이미 갖추고 있다.

- `Document` 중심의 값 모델과 schema 9 직렬화
- `DocumentDelta`와 merge id를 이용한 delta 기반 undo
- seed, frame, sample에 의해 결정되는 상태 없는 스트로크 렌더링
- ordered framebuffer op인 `pixelSelectionOp`과 `reframeOp`
- 마스크 에셋 중복 제거와 직렬화
- 파일 크기, 점 개수, 스트로크 수, 캔버스 크기 등의 검증 한도
- 미리보기, 내보내기, 복구, 자동 저장, 양 플랫폼 패키징
- v1.0.0 schema 9 문서의 픽셀 해시 골든과 Clang-Tidy warnings-as-errors 게이트

다음 항목은 WWT 기능을 참고하더라도 WWP에 새 기반이 필요하다.

| 기능 | 현재 제약 | 필요한 방향 |
| --- | --- | --- |
| Insert Image | RGBA 원본을 보존하는 op과 자산 저장소가 없음 | 별도 raster asset과 image op |
| Smooth 모션 | 프레임별 독립 해시 노이즈이며 시간 보간이 없음 | 주기적인 결정론적 시간 샘플링 |
| Tolerance Fill | 기존 Fill은 알파 경계(`qAlpha >= 128`)만 본다 | legacy Fill 보존과 색 비교 기준 추가 |
| Lasso Paint | 기존 Fill은 polygon이 아니라 seed flood fill | polygon fill 또는 동결된 coverage mask |
| Merge Down | opacity, blend, clip, group 때문에 일반 벡터 병합 불가 | 안전한 부분집합만 벡터 병합 |
| `.wawa` 가져오기 | native binary와 web JSON이 같은 확장자를 사용 | native v10만 선택적으로 변환 |

`MaskAssetTable`은 grayscale 또는 binary mask 전용이다(`canonicalMaskBytes`가 `Format_Grayscale8`이 아니면 빈 값을 낸다). 이미지 자산은 같은 테이블에 넣지 않는다. content hash와 중복 제거 방식만 참고해 별도의 `RasterAssetTable` 또는 타입이 구분되는 자산 계층을 설계한다.

### 이미 존재해서 새로 만들지 않는 것

WWT 기능 목록과 겹치지만 WWP에 이미 구현된 부분이 있다. 새 어휘나 새 저장소를 만들지 말고 아래를 확장한다.

| 항목 | 현재 상태 |
| --- | --- |
| 참조 대상 선택 | `CanvasWandReference{ActiveLayer, ReferenceLayers, AllVisibleLayers}`가 Wand에 구현돼 있고 `drawingTools/wand/reference`로 저장된다. Bucket에는 아직 없다 |
| Fill mask 동결 | `Stroke::fillMask`가 schema 5 보존용으로 남아 있고 직렬화·중복 제거·replay(`applyFillStroke`의 coverage 경로)까지 배선돼 있다. 새 fill에 재사용하기로 결정하면 schema 5 전용이라는 현재 코드 계약도 함께 갱신한다 |
| Fill 경계 페더링 | `applyFillStroke`가 이미 mask 바깥 1픽셀을 기존 알파에 비례해 blend한다 |
| Antialias 플래그 | `BrushSettings::antialiasing`이 있고 bucket이 이미 실어 보내지만 Fill replay는 현재 이 값을 읽지 않는다 |
| 최근 색 | `ColorSwatchRow`가 12칸 최근 색을 QSettings에 저장한다. 저장 팔레트는 없다 |

따라서 Bucket Tolerance는 "참조 대상 고르기"가 아니라 "**비교 기준을 알파에서 색으로 넓히고 Bucket에도 참조 선택을 붙이는**" 작업이고, Bucket Antialias는 신규가 아니라 "기존 경계 페더링과 새 토글의 관계 정리"다.

## 4. 선결 설계 결정

### 결정 A: 기존 렌더 기준선

v1.0.0 태그에서 대표 schema 9 문서 3개를 생성하고 10개 프레임의 canonical RGBA 픽셀 해시를 고정했다. 기존 property test와 별도로 `LegacyRenderGoldenTests`가 이 기준을 검사한다.

기준선에는 다음 문서를 포함한다.

- Paint와 Erase 스트로크
- Fill과 pixel selection op
- 여러 wobble amount와 frame count
- opacity, blend mode, clip, group 조합
- reframe 전후 canvas epoch
- 예제 문서와 복수 레이어 문서

기존 문서는 항상 legacy 렌더 경로를 사용한다. 새 기능의 기본값만으로 legacy 경로를 흉내 내지 않는다.

### 결정 B: WWP식 시간 모델

FPS는 재생과 내보내기의 샘플링 속도로 유지한다. 흔들림 속도를 FPS에 합치지 않는다.

새 시간 모델은 다음 원칙을 따른다.

- frame과 문서 설정만으로 pose를 계산하는 순수 함수다.
- animation frame 범위 안에서 주기적으로 반복된다.
- `Classic`은 현재 프레임별 WWP 렌더를 그대로 사용한다.
- `Smooth`는 인접한 결정론적 pose 사이를 시간축으로 보간하거나 주기적인 연속 노이즈를 사용한다.
- `Stepped`는 `holdFrames` 동안 pose를 유지한다.
- `motionSpeed`는 FPS와 독립적으로 시간축 변화 빈도를 조정한다.
- WWT의 Shake Speed와 Step Speed 상수는 그대로 복제하지 않는다.

Smooth 구현 방식은 기술 spike에서 렌더 안정성, 루프 경계, coverage 비용을 비교한 뒤 확정한다. 순차적으로 누적되는 mutable phase는 도입하지 않는다.

### 결정 C: 흔들림 파라미터의 스코프

새로운 시간·공간 흔들림 설정은 우선 문서 전역으로 둔다. 기존 `BrushSettings::wobbleScale`은 스트로크별 배율로 유지한다.

초기 후보 필드는 다음과 같다.

- motion style: `Classic`, `Smooth`, `Stepped`
- motion speed
- hold frames
- detail
- linked amount
- randomness
- broken line enabled
- break amount
- break range

최종 필드와 범위는 기술 spike 뒤 확정한다. UI 개수를 맞추기 위해 의미가 겹치는 필드를 억지로 유지하지 않는다.

지우개 흔들림은 WWT의 전역 소급 토글을 복제하지 않는다. 지우개 입력 시 사용할 `wobbleScale` 기본값 또는 간단한 도구 옵션으로 제공하고, 기존 지우개 스트로크의 값은 바꾸지 않는다.

### 결정 D: RGBA 자산과 이미지 op

Insert Image는 Clip Studio식 **비파괴 이미지 삽입**이다. WWT의 동작을 옮기지 않는다.

WWT는 삽입 즉시 원본을 레이어 비트맵에 굽고 `Dispose`한다. 축소만 되고(scale 상한 1.0), 이후 "Transform Layer"는 이미지가 아니라 레이어 전체를 변형하므로 같은 레이어의 선까지 함께 변형된다. 레이어가 10개를 넘으면 새 레이어를 만들지 않고 활성 레이어에 그대로 그린다. 이 동작은 참고하지 않는다.

#### 사용자 계약

1. **삽입은 언제나 새 레이어를 만든다.** 활성 레이어에 덧그리지 않는다. Clip Studio의 가져오기와 같다.
2. 새 레이어는 **활성 레이어 바로 위, 같은 group scope**에 놓인다. 활성 레이어가 group 안이면 그 group 안에 만든다.
3. 새 레이어 이름은 원본 파일 이름을 쓴다. 파일 이름은 외부 입력이므로 확장자를 떼고 `maximumLayerNameLength`로 자르며, 비어 있으면 기존 기본 이름 규칙으로 되돌린다.
4. 삽입하면 **원본 RGBA가 자산으로 보존된다.** 화면에 보이는 픽셀은 매번 자산에서 유도한다.
5. 이동·확대·축소·회전·반전은 op에 저장되는 **변환 행렬**이다. 굽지 않는다. 나중에 다시 열어 편집해도 원본에서 다시 샘플링하므로 **반복 변형에 열화가 누적되지 않는다.**
6. **확대를 허용한다.** 상한은 자산 예산과 캔버스 한도로 정한다.
7. 삽입 기본 배치는 캔버스에 맞춰 축소 + 중앙. 캔버스보다 작은 이미지는 원본 크기를 유지한다.
8. `maximumLayers` 또는 `maximumLayerDepth`에 걸리면 활성 레이어에 몰래 그리지 않고 **명확히 거부한다.** WWT는 레이어 10개를 넘으면 조용히 활성 레이어에 그려버린다.
9. 래스터화(변형 확정)는 이 범위에 넣지 않는다. 원본 보존이 기본이고, 확정이 필요해지면 별도 기능으로 평가한다.

#### 표현: 새 `LayerKind`가 아니라 ordered op

삽입이 언제나 새 레이어를 만든다면 `LayerKind::Image`가 자연스러워 보이지만, 그래도 ordered op을 고른다. 삽입 **동작**이 레이어 단위인 것과 **표현**이 레이어 단위여야 하는 것은 별개다.

op으로 두면 "이미지 레이어"는 image op 하나만 든 평범한 Paint 레이어가 되고, 다음이 그대로 성립한다.

- `LayerKind`를 분기하는 모든 지점(hierarchy, dock, thumbnail, composition, merge down 안전 조건, validation)에 새 경우가 생기지 않는다
- 변형 재편집이 `DocumentDelta::ReplacedStroke`로 표현된다. 새 delta 종류가 필요 없다
- canvas epoch, visibilityClip, coverage 경로가 이미 op을 다룰 줄 안다
- 이미지 레이어 위에 바로 선을 그리는 것이 특별한 경우가 아니라 그냥 같은 레이어에 stroke를 추가하는 일이 된다

`LayerKind::Image`는 위 네 가지를 전부 새로 처리해야 하는 대신 얻는 것이 없다. 채택하지 않는다.

레이어 안에서 이미지와 선의 앞뒤 순서를 섞는 것은 기본 흐름이 아니지만 표현상 가능하다. 이건 부수 효과이지 채택 이유가 아니다.

#### 애니메이션에서 이미지는 정지한다

삽입된 이미지는 프레임과 무관하게 같은 픽셀을 낸다. 흔들리지 않는다.

- 프레임마다 큰 이미지를 다시 샘플링하면 preview와 export 양쪽에서 예산을 넘긴다. 60프레임 × 4K 리샘플은 감당할 수 없다
- 변환 행렬이 프레임의 함수가 아니므로 **한 번 샘플링한 결과를 모든 프레임에서 재사용할 수 있다.** 이게 정지를 고르는 실질적 이득이다
- 삽입한 사진이 떠는 것은 이 기능의 목적이 아니다

"이미지도 흔들기"가 필요해지면 별도 기능으로 제안한다. 이 계획에서는 구현하지 않는다.

#### 자산 계약

- canonical pixel format과 alpha 처리
- lossless encoding과 content hash
- 색 공간과 EXIF orientation 정책
- encoded file bytes와 decoded image bytes의 별도 예산
- width, height, stride, 정수 곱셈 overflow 검증
- 중복 자산 제거와 undo·autosave·recovery 수명
- native replay, display-scale replay, coverage, selection, thumbnail 경로
- 변형 결과의 프레임 간 재사용과 무효화 조건

`MaskAssetTable`은 직접 재사용하지 않는다.

#### 선결 조건: 두 예산이 지금 상태로는 이미지를 못 받는다

이 둘을 정하기 전에는 raster asset 구현을 시작하지 않는다.

**문서 크기 한도.** `DocumentLimits::maximumProjectBytes`는 32 MiB이고 저장·로드·자동 저장·복구 전 경로에서 하드 강제된다(`DocumentSerializer`, `RecoveryStore`, `RecoveryWriter`, `PreparedPlanBuilder`). 4096×4096 RGBA는 디코드 기준 64 MiB이고, 무손실 압축 후 base64를 거치면 사진 한 장으로 문서 예산 전체를 소진할 수 있다.

원본 보존이 계약이 된 이상 사이드카/컨테이너 분리는 배제한다 — `.wagle`의 단일 파일 성질이 복구·자동 저장·배포 전체의 전제이기 때문이다. 따라서 **`maximumProjectBytes` 상향 + 삽입 이미지 픽셀 수 상한**의 조합으로 간다. 두 수치는 단계 1에서 실측으로 정한다.

상향이 감당 가능하다고 보는 근거는 압축 payload가 content id로 캐시된다는 점이다(`payloadCacheKey`). 변형만 바꾸면 자산 바이트는 그대로이므로 자동 저장마다 이미지를 다시 압축하지 않는다. 자동 저장은 30초 간격이고 워커에서 돌므로 매 틱 비용은 base64 인코딩과 파일 쓰기가 지배한다. 다만 그 비용도 0은 아니므로 실측이 필요하다.

실측 항목: 4K 사진 1·3·5장에 대한 저장 시간, 자동 저장 1틱 시간, 복구 파일 크기, peak RSS.

**프로세스 메모리 예산.** `MemoryBudget`은 768 MiB 목표에 대해 두 개의 `static_assert`로 이미 꽉 차 있다(history 192 + export 512, 그리고 history + serialization cache + preview 조합). 새 RGBA decode 예산을 추가하면 컴파일이 깨진다. 어느 항목을 줄일지, 아니면 `residentTargetBytes`를 올릴지를 단계 1에서 먼저 정한다.

원본 보존은 history에도 영향을 준다. 변형을 되돌릴 때 자산 바이트까지 복제하면 undo 한 번에 수십 MiB가 잡힌다. 자산은 content id로 공유하고 op의 변환 행렬만 delta에 담아야 한다 — `DocumentDelta`가 payload backing을 공유로 유지하는 기존 방식과 같은 원칙이다.

단계 4의 종료 조건은 이 두 결정 없이는 만족시킬 방법이 없다.

### 결정 E: Fill과 Lasso Paint

schema 9 Fill의 알파 경계 flood fill은 변경하지 않는다.

#### target은 축이 하나가 아니라 둘이다

WWT의 `Bucket Target` 콤보박스는 서로 다른 두 축을 한 컨트롤에 묶어 놓았다.

| WWT 항목 | 샘플링하는 표면 | 비교 기준 |
| --- | --- | --- |
| Color Area | 보이는 레이어 전체 합성 | RGBA 최대 채널 차이에 tolerance 적용 |
| Layer Brush Area | 활성 레이어만 | 알파만, tolerance 하한 2로 고정 |

WWP는 이 둘을 분리해서 노출한다. 묶어서 옮기면 "합성을 보면서 알파로만 채우기" 같은 조합을 표현할 수 없다.

- **참조 대상**: 기존 `CanvasWandReference`(ActiveLayer / ReferenceLayers / AllVisibleLayers)를 Bucket에도 적용한다. 새 2값 enum을 만들지 않는다.
- **비교 기준**: 알파 경계(현재 동작) 또는 색 + 알파 tolerance. tolerance 0은 알파 경계 동작과 같은 결과여야 한다.

자동 선택(Wand)은 이미 참조 대상 축을 갖고 있고 비교 기준 축만 없다. 두 도구가 같은 비교 기준 구현을 공유한다.

#### 무엇을 동결하고 무엇을 절차적으로 두는지

이건 성능 문제가 아니라 애니메이션 의미 문제다. 현재 Fill은 **프레임마다** 그 프레임의 레이어 이미지에 flood fill을 다시 돌린다(`applyFillStroke`). 그래서 채운 영역이 흔들리는 선을 따라 같이 움직인다. 동결하면 그 성질을 잃는다.

- 활성 레이어 + 알파 경계 + tolerance 0: 현재 동작 그대로, **절차적으로 유지한다**
- 다른 레이어나 합성을 참조한 결과: replay 시 재계산하지 않고 저장된 mask로 **동결한다**
- 활성 레이어 + 색 tolerance: **단계 1 spike에서 결정한다.** 색 비교는 프레임마다 결과가 달라질 수 있어 절차적으로 두면 채움 영역이 프레임 간에 튈 수 있고, 동결하면 선을 따라가지 않는다. 두 결과를 실제로 렌더해 보고 고른다

동결 경로는 새로 만들 것이 없다. `Stroke::fillMask`가 schema 5 보존용으로 남아 있고 직렬화·중복 제거·replay까지 이미 배선돼 있으므로, 새 fill이 이 필드를 채우기만 하면 된다.

Lasso Paint는 기존 `StrokeMode::Fill`의 seed flood fill을 재사용하지 않는다. 다음 두 구현을 비교한 뒤 하나를 선택한다.

- 정적인 polygon coverage mask를 동결해 채우기
- polygon 정점을 저장하고 WWP 흔들림으로 렌더하는 `PolygonFill` op

WWT의 `FillPolygon` 결과를 재현할 필요는 없다.

### 결정 F: Merge Down

벡터를 유지하는 Merge Down은 다음과 같은 안전한 경우에만 제공한다.

- 같은 sibling scope의 paint layer
- Normal blend
- opacity 1
- clipping 없음
- 호환 가능한 canvas epoch
- 두 레이어의 `reference` 플래그가 같음
- 병합 후 문서 한도를 넘지 않음

`reference`는 Wand의 `ReferenceLayers` 모드가 참조하는 값이다. 값이 다른 두 레이어를 병합하면 병합 자체는 성공하면서 Wand 동작만 조용히 바뀐다.

그 밖의 경우에는 명확한 비활성 사유를 표시한다. raster flatten fallback은 image op 기반이 안정된 뒤 별도 기능으로 검토한다.

### 결정 G: `.wawa`는 선택적 마이그레이션

`.wawa` 가져오기는 핵심 확장과 분리한다. 지원한다면 Windows native binary 버전 10만 대상으로 하고 다음 계약을 따른다.

- `.NET BinaryReader` 형식의 길이 prefix 문자열과 little-endian 수치를 정확히 읽는다.
- web JSON `.wawa`는 지원하지 않고 명확한 오류를 표시한다.
- base bitmap은 RGBA 자산으로 가져온다.
- stroke, eraser, fill은 가능한 범위에서 WWP 동작으로 근사 변환한다.
- WWT와 픽셀 또는 애니메이션이 동일하다고 보장하지 않는다.
- 변환되거나 생략된 항목을 import summary로 알린다.
- 가져온 결과는 미저장 새 문서로 열고 원본 경로를 현재 저장 경로로 사용하지 않는다.
- 첫 저장은 같은 basename의 `.wagle`을 제안하는 Save As로 보낸다.

실제 사용자 파일이 확보되지 않으면 importer를 구현하지 않는다.

## 5. 기능 우선순위

| 그룹 | 기능 | 우선순위 | 비고 |
| --- | --- | --- | --- |
| 선 표현 | Detail | 핵심 | 현재 detail 상수의 안전한 파라미터화 |
| 선 표현 | Smooth / Stepped | 핵심 | WWP식 결정론적 시간 모델 |
| 선 표현 | Motion Speed / Hold Frames | 핵심 | FPS와 분리 |
| 선 표현 | Linked / Randomness | 핵심 | 새 노이즈 채널 사용 |
| 선 표현 | Broken Line | 핵심 | 분리된 visible run과 coverage 필요 |
| 채색 | Bucket 비교 기준(색/알파 tolerance) | 핵심 | legacy Fill은 그대로 유지 |
| 채색 | Bucket 참조 대상 | 핵심 | Wand의 기존 3값 enum을 재사용 |
| 채색 | Bucket Antialias | 낮음 | 신규 아님. 기존 경계 페더링과의 관계 정리 |
| 선택 | Invert Selection / Select All | 완료 | `EDIT_BEHAVIOR_ALIGNMENT_PLAN.md` 단계 B에서 구현 완료 |
| 자산 | Insert Image | 핵심 | 비파괴 삽입. 결정 D의 두 예산이 선결 조건 |
| 도구 | Lasso Paint | 중요 | polygon 표현 결정 필요 |
| 레이어 | 제한된 Merge Down | 중요 | 안전 조건에서만 벡터 유지 |
| 색상 | Palette | 중요 | 최근 색과 저장 팔레트 역할 분리 |
| 설정 | WWP Preset | 중요 | WWP 자체 포맷 우선 |
| 입출력 | Animated WebP | 선택 | loop, alpha, frame timing 명시 |
| 파일 | Native v10 `.wawa` import | 선택 | best-effort migration |
| 설정 | Legacy palette/preset import | 선택 | 실제 파일 수요 확인 후 결정 |
| UI | Theme Color / Help | 선택 | 핵심 기능과 독립 배포 가능 |

## 6. 스키마와 호환성 정책

### 공개 스키마는 기능 묶음별로 한 번씩 정의한다

schema 10은 motion 모델과 새 Fill/Lasso 표현을 담당하고, schema 11은 raster asset과 image op을 담당한다. 가장 비싸고 불확실한 Insert Image가 선 표현 배포를 막지 않게 하되, 각 스키마 안에서는 중간 형식을 외부에 배포하지 않고 한 번만 고정한다.

두 스키마의 공통 작업에는 다음이 모두 포함되어야 한다.

- `Document` 필드와 기본값
- limits와 validation
- JSON codec과 round-trip
- `DocumentDelta`, apply, equality, merge id
- controller setter와 modified 상태
- history memory 계산
- prepared plan identity와 cache invalidation
- immutable backing과 freezer
- incremental serialization 비교
- 렌더 metadata reuse 조건

schema 11에는 raster asset의 저장·중복 제거·budget과 image op의 자산 수명 계약이 추가된다. 기술 spike 동안 내부 형식은 바뀔 수 있지만 공개된 schema 10이나 schema 11의 의미를 prototype 편의 때문에 바꾸지 않는다.

### legacy 경로를 명시적으로 보존한다

- schema 9 문서는 현재 렌더와 Fill 알고리즘을 그대로 사용한다.
- schema 10이나 schema 11에서 새 기능을 사용하지 않은 문서도 v1.0 골든과 동일해야 한다.
- 기존 noise channel 상수는 변경하거나 다른 용도로 재사용하지 않는다.
- 신규 노이즈 용도에는 고유한 channel 상수를 배정한다.

`algorithmVersion`은 파일에 기록되고 로드 시 검사된다. 현재 값보다 큰 파일은 거부되므로, 이 값을 올리면 기존 빌드가 새 파일을 열 수 없게 된다. 유지 여부는 골든 테스트로 결정한다. 렌더 경로가 조금이라도 달라지면 버전 값을 단순히 유지하지 않고 legacy 알고리즘 선택을 문서 모델에 명시적으로 보존한다.

`serializationFormatGeneration`은 호환성 값이 아니다. 디스크에 쓰이지 않는 프로세스 로컬 압축 캐시 네임스페이스이므로 판단이 필요한 결정이 아니라 규칙이다 — payload 인코딩이 바뀌면 올린다. 이 값을 `algorithmVersion`과 같은 무게로 다루지 않는다.

## 7. 구현 단계

### 단계 0: 기준선과 계약

예상 기간: 1~2주

- [x] v1.0.0 schema 9 렌더 골든 corpus 생성
- [x] Clang-Tidy diagnostics를 warnings-as-errors로 올리고 macOS CI 게이트 연결
- [ ] 기능별 포함·제외와 사용자 동작 계약 확정
- [ ] WWP식 Smooth / Stepped 시간 모델 후보 정의
- [ ] raster asset의 pixel format과 메모리 예산 정의
- [ ] legacy Fill과 새 Bucket의 경계 확정
- [ ] Merge Down 안전 조건 확정
- [ ] WWT 코드나 자산을 직접 반입할 항목이 있으면 provenance 확인

종료 조건:

- 기존 렌더 회귀를 자동으로 감지할 수 있음
- schema에 들어갈 개념과 제외할 기능이 명확함
- spike에서 결정할 항목, 비교 방법, 합격 기준이 빠짐없이 식별됨

### 단계 1: 기술 spike와 데이터 모델

예상 기간: 2~3주

- [ ] Smooth 시간 보간 또는 연속 노이즈 prototype
- [ ] Stepped와 Hold Frames의 루프 경계 검증
- [ ] Broken Line의 visible run, coverage, incremental render prototype
- [ ] `RasterAssetTable`과 image op prototype
- [ ] `maximumProjectBytes`와 `MemoryBudget` 결정 (결정 D의 선결 조건)
- [ ] 활성 레이어 + 색 tolerance를 절차적으로 둘지 동결할지 렌더 비교로 결정
- [x] `DocumentDelta::mergeScalar`를 필드 집합 기반으로 일반화
- [ ] frozen Fill mask와 PolygonFill 후보 비교
- [ ] schema 10 motion/Fill 필드와 operation 확정
- [ ] schema 11 raster asset/image op 경계 초안 확정

`mergeScalar`는 document와 layer 스칼라 필드 집합을 각각 한 번만 선언하고, 선택된 필드 하나만 바뀐 delta인지 공통 검사한다. 새 필드는 해당 집합과 merge id 매핑에 한 번씩만 추가하며, 서로 다른 슬라이더가 한 undo 항목으로 합쳐지지 않는 회귀 테스트를 유지한다.

종료 조건:

- preview, export, tile redraw가 같은 frame에서 동일한 결과를 냄
- loop 경계에서 정의되지 않은 pose가 없음
- 대표 RGBA vertical slice로 렌더·undo·저장·로드의 시간·파일·peak memory 비용을 측정할 수 있음
- 새 스칼라 필드를 추가해도 merge id 조건이 선형으로만 늘어남
- schema 10과 schema 11을 각각 한 번 정의할 근거가 확보됨

### 단계 2: 선 표현

예상 기간: 3~5주

- [ ] Detail
- [ ] Motion Style과 Motion Speed
- [ ] Hold Frames
- [ ] Linked와 Randomness
- [ ] Broken Line, Break Amount, Break Range
- [ ] 지우개 wobble 입력 옵션
- [ ] 흔들림 전용 UI와 기존 타임라인 컨트롤 이전
- [ ] coverage, selection, incremental render 테스트

Broken Line은 이 단계에서 가장 비싸다. 나머지 항목이 문서 스칼라와 순수 함수만 건드리는 반면, gap이 생기면 `StrokeCoverageRenderer`의 primitive 단위 bounds와 incremental redraw 경로를 함께 고쳐야 한다. 일정이 밀리면 Broken Line을 먼저 뒤로 미루고 나머지를 먼저 닫는다.

종료 조건:

- Classic 기본값에서 v1.0 골든이 픽셀 단위로 동일함
- 모든 새 pose가 seed와 frame에 대해 결정론적임
- Smooth, Stepped, Broken의 루프와 부분 재그리기가 안정적임
- WWT와 닮았는지가 아니라 WWP 안에서 조작 결과가 일관되고 유용함

### 단계 3: 채색과 선택

예상 기간: 2~4주

- [ ] Bucket 비교 기준: 알파 경계와 색 tolerance
- [ ] Bucket에 `CanvasWandReference` 적용
- [ ] Wand와 Bucket이 같은 비교 기준 구현을 공유
- [ ] Bucket Antialias와 기존 경계 페더링의 관계 정리
- [ ] Lasso Paint
- [ ] 안전 조건의 Merge Down

종료 조건:

- 기존 Fill 문서의 골든이 바뀌지 않음
- tolerance 0이 기존 알파 경계 fill과 픽셀 단위로 같음
- 반투명 영역을 tolerance에 따라 예측 가능하게 채울 수 있음
- 다른 레이어를 참조한 결과가 저장·로드 후 동일함
- 색 tolerance fill의 프레임 간 동작이 결정 E에서 정한 정책과 일치함
- `reference` 플래그가 다른 레이어를 포함해 지원하지 않는 Merge Down 조합이 안전하게 거부됨

### 단계 4: 이미지 자산과 Insert Image

예상 기간: 4~6주

단계 1에서 두 예산이 확정되지 않았으면 시작하지 않는다.

- [ ] schema 11 공개 형식 확정
- [ ] raster asset 직렬화와 content hash dedup
- [ ] image op의 전체 replay와 cache 경로 통합
- [ ] 변환 행렬 저장과 **재편집** — 굽지 않는 변형
- [ ] 프레임 간 샘플링 결과 재사용과 무효화 조건
- [ ] 새 레이어 생성과 배치: 활성 레이어 바로 위, 같은 group scope
- [ ] 파일 이름을 레이어 이름으로 사용 (확장자 제거, 길이 제한, 빈 이름 대체)
- [ ] 삽입 기본 배치: 축소 + 중앙, 캔버스보다 작으면 원본 크기
- [ ] 확대 허용과 상한
- [ ] `maximumLayers`·`maximumLayerDepth` 도달 시 명확한 거부
- [ ] 이미지 디코드 한도와 malformed input 테스트
- [ ] EXIF orientation과 색 공간 처리
- [ ] undo, autosave, recovery, copy/delete 자산 수명 테스트

종료 조건:

- 외부 이미지를 넣고 변환한 뒤 저장·재로드할 수 있음
- 삽입이 활성 레이어를 절대 수정하지 않고, group 안에서도 올바른 위치에 새 레이어가 생김
- 레이어 한도에 걸린 삽입이 문서를 전혀 바꾸지 않고 거부됨
- **확대 → 축소 → 확대로 반복 변형해도 원본에서 한 번 샘플링한 것과 품질이 같음**
- **저장·재로드 후에도 변형을 이어서 편집할 수 있음**
- 삽입된 이미지가 모든 프레임에서 동일한 픽셀을 냄
- 변형만 되돌리는 undo가 자산 바이트를 복제하지 않음
- 중복 이미지가 불필요하게 반복 저장되지 않음
- 손상되거나 과대한 이미지가 현재 문서를 바꾸지 않고 거부됨
- 정한 픽셀 수 상한까지의 이미지와 여러 레이어에서 정한 peak memory 예산을 지킴

### 단계 5: 선택 기능

각 기능은 독립적으로 산정하고 핵심 확장과 별도로 배포할 수 있다.

- [ ] Palette와 WWP Preset
- [ ] Animated WebP
- [ ] Theme Color와 앱 내 Help
- [ ] 실제 수요가 있을 때 native v10 `.wawa` importer
- [ ] 실제 파일이 확보됐을 때 legacy palette/preset importer

`.ini` 번역 패치와 WAV 재생은 이 계획에서 구현하지 않는다. 필요성이 생기면 별도 제안서로 평가한다.

## 8. 테스트 전략

### 기존 동작 회귀

- v1.0.0에서 생성한 schema 9 문서의 프레임별 render hash 또는 이미지 비교
- 기존 `WobbleAnimationTests`, `StrokeRenderingTests`, `StrokeCoverageTests` 유지
- preview, export, thumbnail, tile redraw 사이의 결과 비교
- schema 9 load와 schema 10·11 round-trip
- 변경한 C++ 묶음마다 `wobblepaint_format_check`, `wobblepaint_tidy`, 관련 테스트 실행

`WobbleAnimationTests`의 반복성·변화 여부·wobbleScale property test 다섯 개는 그대로 유지한다. 골든 corpus는 이를 대체하지 않고 `LegacyRenderGoldenTests`에서 별도로 검사한다.

### 새 기능

- 각 motion style의 loop, hold, speed, zero amount, extreme range
- Broken Line의 gap, coverage bound, 선택 표시, incremental redraw
- **tolerance 0이 기존 알파 경계 fill과 픽셀 단위로 같은 결과를 낼 것**
- tolerance 최대값, alpha-only와 RGBA 경계, 참조 대상 3종
- 색 tolerance fill을 여러 프레임에서 렌더했을 때 채움 영역이 정한 정책(절차적 또는 동결)대로 동작할 것
- PolygonFill 또는 frozen lasso mask의 저장·undo·재생
- 새 흔들림 슬라이더를 연속으로 드래그했을 때 서로 다른 필드가 한 undo 항목으로 합쳐지지 않을 것
- raster asset dedup, 삭제, copy, undo/redo, recovery
- **반복 변형 무열화**: 확대 → 축소 → 확대한 결과가 같은 최종 행렬을 한 번에 적용한 결과와 픽셀 단위로 같을 것
- 저장·재로드 후 변형을 이어서 편집했을 때 위와 같을 것
- 삽입된 이미지가 전 프레임에서 동일할 것
- 변형만 바꾼 undo 항목이 자산 바이트를 복제하지 않을 것
- Animated WebP의 alpha, loop, frame duration, cancellation

### 외부 입력 안전성

- 파일 크기, 이미지 크기, 레이어·스트로크·점·문자열 누적 한도
- 정수 overflow와 NaN/Inf 좌표
- 잘린 파일, 잘못된 길이, trailing bytes
- PNG/JPEG/WebP decode bomb 방지
- 실패 시 현재 문서가 부분적으로 교체되지 않는 atomic import

### 수동 검증

1. Classic 문서를 여러 frame에서 비교해 v1.0과 동일한지 확인한다.
2. Smooth와 Stepped를 같은 FPS에서 바꿔 motion style과 playback speed가 독립적인지 확인한다.
3. 반투명 선으로 닫힌 영역을 만들고 tolerance와 target을 바꿔 채운다.
4. Lasso Paint와 Merge Down을 여러 blend·clip·group 상태에서 검증한다.
5. 큰 이미지를 삽입하고 transform, undo, save, recovery를 반복한다.
6. 선택적 `.wawa` importer가 있을 경우 변환 요약과 원본 비덮어쓰기를 확인한다.

WWT와의 화면 비교는 기능 아이디어를 검토하는 참고 시나리오일 뿐 통과 조건이 아니다.

## 9. UI 원칙

흔들림 컨트롤은 타임라인에 모두 넣지 않고 전용 팝오버 또는 도크로 분리한다.

- 타임라인: frame count, current frame, FPS, play/stop
- 흔들림 UI: amount, motion style, motion speed, hold, detail, linked, randomness, broken 설정
- 도구 UI: 현재 도구에만 적용되는 wobble scale, fill tolerance, 참조 대상, antialias

이건 추가가 아니라 **이전**이다. `wobbleAmount` 슬라이더와 스핀박스, 그리고 `WobblePreview` 위젯이 현재 `TimelineBar` 안에 있다. 새 팝오버를 만든 뒤 타임라인에서 걷어내는 작업이고, `UiShellTests`와 `UiSessionTests`의 위젯 탐색이 함께 깨진다. 단계 2 산정에 포함한다.

WWT의 좌측 슬라이더 패널을 재현하지 않는다. WWP의 기존 팝오버, 키보드 접근성, 작은 창과 HiDPI 동작을 우선한다.

필드 수는 WWT의 설정 개수를 맞추는 기준으로 정하지 않는다. 서로 다른 필드가 같은 사용자 효과를 내면 WWP에서는 하나로 합친다.

## 10. 주요 위험과 대응

| 위험 | 영향 | 대응 |
| --- | --- | --- |
| legacy renderer 내부를 직접 수정 | 기존 `.wagle`의 그림이 바뀜 | Classic 경로 분리와 v1.0 골든 비교 |
| FPS와 motion speed 혼합 | 재생 속도와 흔들림 성격을 독립적으로 조절할 수 없음 | 문서 필드를 분리하고 순수 phase 함수 사용 |
| mask storage에 RGBA 저장 | 타입·색상·예산 계약이 깨짐 | 별도 raster asset 계층 구현 |
| Fill 함수를 전역 교체 | schema 9 Fill 결과가 바뀜 | legacy Fill 유지, 새 결과는 mask로 동결 |
| image op 영향 범위 누락 | preview는 되지만 undo·save·recovery가 깨짐 | 전체 replay/cache/history 체크리스트 사용 |
| 일반 Merge Down에서 벡터 보존 약속 | blend·clip·group에서 픽셀 결과가 바뀜 | 안전한 부분집합만 지원 |
| WWT 동작을 그대로 정답으로 사용 | 실험 구현의 버그와 모순까지 복제 | WWP의 기능 계약과 테스트를 기준으로 재설계 |
| `.wawa`를 일반 Open으로 처리 | Save가 원본 파일을 덮어쓸 수 있음 | 미저장 변환 문서와 강제 Save As |
| 외부 이미지와 importer의 입력 한도 누락 | 메모리 고갈 또는 손상 문서 | 누적 예산, decode 제한, atomic import |
| 32 MiB 문서 한도를 그대로 둔 채 Insert Image 착수 | 이미지 한 장으로 저장·자동 저장이 실패 | 결정 D의 두 예산을 단계 1에서 먼저 확정 |
| 변형을 굽는 편의 구현 | 반복 변형에 열화가 누적되고 재편집이 불가능해짐 | 변환 행렬만 저장, 표시 픽셀은 항상 원본에서 유도 |
| 이미지 자산을 undo delta에 값으로 담음 | 변형 한 번에 수십 MiB가 history에 잡힘 | content id로 공유, delta에는 행렬만 |
| 새 흔들림 필드에 merge id를 개별 추가 | 다른 슬라이더끼리 한 undo 항목으로 합쳐짐 | 필드 추가 전에 `mergeScalar` 일반화 |
| 채색 결과 동결 범위를 넓게 잡음 | 채운 영역이 선을 따라 흔들리지 않게 됨 | 참조 대상별로 동결·절차 구분, spike에서 렌더 비교 |
| 공개 스키마를 너무 일찍 배포 | 후속 op 때문에 같은 스키마의 의미를 바꾸게 됨 | 기능 묶음별 spike 완료 뒤 schema 10과 11을 각각 고정 |

## 11. 일정 가정

기술 spike 전의 일정은 약속이 아니라 범위 산정을 위한 초기 추정치다.

| 단계 | 기간 |
| --- | --- |
| 0. 기준선과 계약 | 1~2주 |
| 1. 기술 spike와 데이터 모델 | 2~3주 |
| 2. 선 표현 | 3~5주 |
| 3. 채색과 선택 | 2~4주 |
| 4. 이미지 자산과 Insert Image | 4~6주 |
| 핵심 합계 | 12~20주 |
| 5. 선택 기능 | 기능별 별도 산정, 전체 3~6주 예상 |

핵심 범위와 선택 기능을 모두 구현하면 초기 총합은 15~26주다. 단계 1의 spike가 끝나면 실제 변경 파일 수, 메모리 비용, 테스트 결과를 기준으로 다시 산정한다.

선 표현과 새 Fill/Lasso 표현은 완결된 schema 10으로 먼저 배포할 수 있다. RasterAsset과 image op은 예산과 자산 수명 계약까지 닫힌 schema 11로 뒤따른다. 두 버전 모두 feature flag가 아니라 읽기·쓰기·legacy 경로·round-trip이 완결된 공개 형식이어야 한다.

## 12. 첫 구현 묶음

첫 묶음은 사용자 기능이 아니라 회귀 기준과 설계 위험을 줄이는 작업이다.

1. [x] v1.0.0 렌더 골든 corpus 생성
2. [x] Clang-Tidy warnings-as-errors와 CI 게이트 확정
3. [ ] Classic 렌더 경로의 변경 금지 경계 확정
4. [ ] `maximumProjectBytes`와 `MemoryBudget` 결정 — Insert Image 착수의 선결 조건
5. [x] `DocumentDelta::mergeScalar` 일반화 — 새 문서 필드 추가의 선결 조건
6. [ ] Smooth와 Stepped 시간 모델 prototype 비교
7. [ ] `RasterAssetTable`의 canonical format과 예산 prototype
8. [ ] frozen Fill mask와 PolygonFill 표현 비교
9. [ ] 문서 필드와 operation 목록 확정
10. [ ] schema 10/11 경계와 전체 integration checklist 작성

3번과 4번은 다른 항목의 결과를 기다리지 않는다. 둘 다 미해결인 상태에서 단계 2 이후를 시작하면 나중에 되돌리는 비용이 커진다.

이 묶음이 끝나기 전에는 schema 10이나 schema 11 파일을 배포하지 않는다.

## 13. 완료 정의

- 우선 범위의 기능이 구현됐거나 제품 판단에 따라 명시적으로 제외되었다.
- WWT 코드의 구조나 설정 개수가 아니라 WWP의 사용성과 유지보수성을 기준으로 기능이 설계되었다.
- schema 9 대표 문서가 v1.0.0 골든과 픽셀 단위로 동일하다.
- 새 시간 모델이 preview, export, partial redraw에서 결정론적으로 동작한다.
- Fill, Lasso Paint, image op이 저장·로드·undo·recovery를 통과한다.
- 삽입한 이미지의 원본이 보존되고, 반복 변형과 재로드 후 재편집에 열화가 누적되지 않는다.
- 외부 입력이 정한 크기와 메모리 예산을 지키며 실패가 원자적이다.
- Merge Down은 지원 가능한 조건을 명확히 검사한다.
- 선택적 `.wawa` importer가 구현된 경우 원본을 덮어쓰지 않고 변환 한계를 사용자에게 알린다.
- 자동 테스트, 양 플랫폼 패키지 smoke test, 작은 창과 HiDPI 수동 검증이 통과한다.
