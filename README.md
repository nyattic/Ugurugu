# WobblePaint

WobblePaint는 그린 선의 중심을 유지하면서 외곽이 부드럽게 떨리는 애니메이션 그림 도구입니다. 문서를 벡터 스트로크로 보관하고 각 프레임을 결정론적인 순환 노이즈로 다시 렌더링합니다.

## 현재 기능

- 브러시와 레이어 단위 지우개
- 시간적으로 부드럽고 완전히 순환하는 선 떨림 미리보기
- 레이어 추가, 복제, 삭제, 순서, 이름, 표시, 불투명도
- 스트로크와 레이어 편집 Undo/Redo
- 압력 값을 포함한 태블릿 입력
- 확대, 축소, 화면 맞춤, Space 또는 가운데 버튼 이동
- 버전이 지정된 `.wobble` 프로젝트 저장과 불러오기
- 공통 적응형 팔레트를 사용하는 무한 반복 GIF 출력
- 현재 프레임 PNG 출력
- 콘솔 및 회전 파일 로그

## 기술 구성

- C++23
- Qt 6.10 이상
- spdlog 1.13 이상
- CMake 3.25 이상
- Ninja 또는 Visual Studio 빌드 도구

프로젝트가 직접 소유하는 모든 헤더는 `.hpp` 확장자를 사용합니다.

## macOS 빌드

```sh
brew install cmake ninja qt spdlog
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
open out/build/macos-debug/WobblePaint.app
```

릴리스 빌드:

```sh
cmake --preset macos-release
cmake --build --preset macos-release
cmake --install out/build/macos-release --prefix out/install/macos
cmake --build out/build/macos-release --target package
```

## Windows 빌드

Qt 6의 MSVC 2022 x64 패키지, Visual Studio 2022, CMake와 spdlog CMake 패키지가 필요합니다. spdlog는 vcpkg로 설치할 수 있습니다.

```powershell
vcpkg install spdlog:x64-windows
cmake --preset windows-debug -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows-debug
ctest --preset windows-debug
```

릴리스 설치 단계는 Qt 런타임도 함께 배치합니다.

```powershell
cmake --preset windows-release -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build --preset windows-release
cmake --install out/build/windows-release --config Release --prefix out/install/windows
cmake --build --preset windows-release --target package
```

Qt 경로는 설치한 버전에 맞게 바꿔야 합니다.

## 기본 조작

- `B`: 브러시
- `E`: 지우개
- `P`: 애니메이션 미리보기 재생 또는 정지
- `Current`: 현재 프레임 선택, 값을 바꾸면 미리보기 자동 정지
- `Ctrl/Cmd+Z`: Undo
- `Ctrl/Cmd+Shift+Z`: Redo
- `Ctrl/Cmd+0`: 캔버스 화면 맞춤
- 마우스 휠: 확대와 축소
- Space+드래그 또는 가운데 버튼 드래그: 캔버스 이동
- `Esc`: 진행 중인 스트로크 취소

Wobble 값은 선 중심축에서 움직이는 최대 진폭의 기준값입니다. Frames와 FPS를 함께 조절하면 루프 길이와 움직임의 부드러움을 바꿀 수 있습니다.

`examples/Wave.wobble`을 열면 떨림과 압력 변화가 적용된 중립적인 예시 문서를 바로 확인할 수 있습니다.

## 로그

로그는 콘솔과 Qt의 플랫폼별 `AppLocalDataLocation` 아래 `WobblePaint.log`에 기록됩니다. 파일은 2MB 단위로 회전하며 최근 3개를 유지합니다.

## 테스트

자동 테스트는 문서 직렬화, Undo/Redo, 레이어 합성 지우개, 결정론적 루프 렌더링, 점과 중복 좌표 안정성, 애니메이션 GIF 인코딩과 디코딩을 검증합니다.
