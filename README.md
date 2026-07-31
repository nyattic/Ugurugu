<p align="center">
  <img src="resources/icons/WobblePaint.png" width="128" alt="WagleWaglePaint 앱 아이콘">
</p>

# WagleWaglePaint

[![Latest Release](https://img.shields.io/github/v/release/nyattic/WagleWaglePaint?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/nyattic/WagleWaglePaint/total?style=for-the-badge&logo=github&logoColor=white&labelColor=1e1b2e&color=ffc94a)](https://github.com/nyattic/WagleWaglePaint/releases)
![License](https://img.shields.io/badge/license-GPL--3.0-ffc94a?style=for-the-badge&logo=gnu&logoColor=white&labelColor=1e1b2e)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-ffc94a?style=for-the-badge&logo=qt&logoColor=white&labelColor=1e1b2e)

<p align="center"><b>KR</b> · <a href="README.en.md">EN</a> · <a href="README.ja.md">JP</a></p>

모든 선이 꿈틀거리는 네이티브 드로잉 앱입니다. 한 번만 그리면
스케치가 살아 있는 듯한 보일링 라인 애니메이션으로 깨어나고,
반복 재생되는 GIF로 바로 내보낼 수 있습니다.

Shake Art DELUXE와 PS1 스타일 버텍스 지터에서 영감을 받았으며,
레이어, 타블렛 필압, 선택 도구, 프로젝트 파일, 자동 업데이트를
지원합니다.

> [!WARNING]
> WagleWaglePaint는 현재 베타 버전입니다. 사용 중 버그나 다듬어지지
> 않은 부분을 만날 수 있습니다. 버그 리포트는 언제나 환영합니다 —
> [GitHub 이슈](https://github.com/nyattic/WagleWaglePaint/issues)에
> 어떤 작업을 했는지, 어떤 결과를 기대했는지, 실제로는 어떻게
> 동작했는지 적어 주세요. `.wagle` 프로젝트 파일을 함께 첨부해 주시면
> 원인을 찾는 데 큰 도움이 됩니다.

## 다운로드

아래 파일 이름을 누르면 최신 버전이 바로 다운로드됩니다. 전체 목록은
[GitHub Releases](https://github.com/nyattic/WagleWaglePaint/releases/latest)에서
볼 수 있습니다.

| 플랫폼 | 지원 환경 | 다운로드 |
| --- | --- | --- |
| Windows | Windows 10 이상, x64 | [WagleWaglePaint-Windows-x64-Setup.exe](https://github.com/nyattic/WagleWaglePaint/releases/latest/download/WagleWaglePaint-Windows-x64-Setup.exe) |
| macOS | macOS 14 이상, Apple Silicon | [WagleWaglePaint-macOS-arm64.dmg](https://github.com/nyattic/WagleWaglePaint/releases/latest/download/WagleWaglePaint-macOS-arm64.dmg) |

릴리즈 페이지의 `.zip`, `.nupkg`, `appcast.xml`, `.json` 파일은 자동
업데이트에 사용되는 파일입니다. 일반 설치에는 필요하지 않습니다.

## 설치

### Windows

다운로드한 Setup 파일을 실행하세요. WagleWaglePaint가 현재 사용자
계정에 설치되고, 설치가 끝나면 자동으로 실행됩니다.

### macOS

DMG를 열고 WagleWaglePaint를 Applications 폴더로 드래그하세요.

현재 빌드는 신뢰할 수 있는 개발자 인증서로 서명되거나 공증되지
않아, Windows SmartScreen이나 macOS Gatekeeper가 경고를 표시할 수
있습니다. 앱은 반드시 공식 Releases 페이지에서만 다운로드하세요.
macOS에서는 앱을 Control-클릭한 뒤 **열기**를 선택하고, 필요하면
확인 창을 승인하세요.

## 기능

- 타블렛 필압을 지원하는 브러시와 지우개
- 펜, 마커, 에어브러시, 스프레이 내장 프리셋 17종
- 손그림 애니메이션 도구에서 영감을 받은 또렷한 픽셀 경계 스트로크
- 부드러운 선을 위한 스트로크별 안티앨리어싱 옵션
- 와글거림 애니메이션을 끄고 일반 그림툴처럼 쓰는 모드
- 프레임마다 독립적으로 다시 그려지는, 완전히 반복 가능한 보일링
  라인 워블
- 선택 영역 아래 액션바에서 이동, 크기 변경, 회전, 좌우·상하 반전을
  미리 본 뒤 한 번에 적용하거나 취소하고, 복제, 삭제, 선택 해제와
  실행 취소를 지원하는 올가미 선택과 자동 선택
- 선택 영역을 인식하는 브러시, 지우개, 페인트 통 편집
- 절대·상대 크기, 3×3 기준점과 오프셋을 지정해 그림을 유지한 채
  캔버스를 자르거나 확장하는 캔버스 영역 크기 변경
- 정확한 픽셀 크기나 배율을 지정하고, 필요하면 가로세로 비율을 유지해
  그림과 브러시 크기를 함께 바꾸는 이미지 크기 변경
- 1~1600% 로그 줌 슬라이더, 실제 픽셀 100% 보기와 창 맞춤
- 섬네일, 표시 여부, 불투명도, 드래그 순서 변경을 지원하며 마지막
  레이어까지 삭제해 레이어가 0개인 빈 캔버스로 둘 수 있는 레이어
- 프레임 스크러버와 실시간 워블 미리보기가 있는 타임라인에서 워블
  강도, 프레임 수, FPS 조절
- 전역 워블 위에 얹는 스트로크별 거칠기 조절
- 반복 GIF 내보내기와 현재 프레임 PNG·JPG 내보내기
- 실행 취소·다시 실행을 지원하는 `.wagle` 프로젝트 파일
- 비정상 종료 후 저장하지 않은 작업 자동 복구
- 앱을 다시 실행해도 유지되는 마지막 도구, 색상, 브러시 프리셋,
  프리셋별 펜 크기, 지우개 크기, 거칠기와 안티앨리어싱
- 최근 색상 유지와 기본 저장 폴더 설정
- 단축키 사용자화, 한국어·영어·일본어 인터페이스
- macOS는 Sparkle, Windows는 Velopack을 통한 자동 업데이트

## 조작법

아래는 기본 단축키입니다. **설정 → 단축키**에서 변경할 수 있습니다.

| 키 | 동작 |
| --- | --- |
| `B` | 브러시 |
| `E` | 지우개 |
| `L` | 올가미 선택 |
| `W` | 자동 선택 |
| `G` | 페인트 통 |
| `P` | 미리보기 재생/일시정지 |
| `M` | 캔버스 좌우 반전 (보기 전용) |
| `Space` + 드래그 | 캔버스 이동 |
| 스크롤 | 확대/축소 |
| `Ctrl/Cmd++` / `Ctrl/Cmd+-` | 확대 / 축소 |
| `Ctrl/Cmd+Space` + 드래그 | 펜이나 마우스로 확대/축소 (오른쪽으로 드래그하면 확대) |
| 상태 표시줄 줌 슬라이더 | 1~1600% 확대/축소 |
| `Alt` + 클릭 | 캔버스에서 색상 추출 (브러시, 지우개, 페인트 통 도구) |
| `Ctrl+Z` / `Ctrl+Y` | 실행 취소 / 다시 실행 (Windows) |
| `Cmd+Z` / `Cmd+Shift+Z` | 실행 취소 / 다시 실행 (macOS) |
| `Ctrl/Cmd+E` | 애니메이션 GIF 내보내기 |
| `Ctrl/Cmd+D` | 선택 영역 복제 |
| `Ctrl/Cmd+0` | 캔버스를 창에 맞추기 |
| `Ctrl/Cmd+1` | 실제 픽셀 크기인 100%로 보기 |
| `Enter` | 대기 중인 선택 변환 적용 |
| `Esc` | 현재 스트로크나 선택 변환 취소, 그 외에는 선택 해제 |

## 설정

툴바의 톱니바퀴 버튼으로 설정을 열 수 있습니다. Windows에서는
**편집 → 설정**, macOS에서는 **WagleWaglePaint → 설정**에서도 열 수
있습니다.

- **일반:** 인터페이스 언어를 선택합니다. 언어를 변경한 뒤에는 앱을
  다시 시작하세요.
- **그리기:** 와글거림 애니메이션을 켜고 끄고, 그리는 동안의
  애니메이션 동작을 선택합니다.
- **파일:** 저장·내보내기 대화 상자가 사용할 기본 폴더를 지정합니다.
- **단축키:** 모든 단축키를 원하는 키로 바꾸고, 필요하면 기본값으로
  되돌릴 수 있습니다.
- **정보:** 현재 설치된 WagleWaglePaint 버전을 확인합니다.

## 자동 업데이트

WagleWaglePaint는 실행 후 업데이트를 확인합니다. **도움말 →
업데이트 확인**으로 언제든 직접 확인할 수도 있습니다. 업데이트는
macOS에서는 Sparkle, Windows에서는 Velopack을 통해 다운로드·설치되며,
업데이트 알림에서 새 버전의 릴리즈 노트를 확인할 수 있습니다.

## 개발자를 위한 안내

소스 빌드와 테스트 방법은 [BUILDING.md](BUILDING.md)를 참고하세요.

## 라이선스

WagleWaglePaint는 [GNU General Public License v3.0](LICENSE)으로
배포됩니다. 내장된 Pretendard JP 폰트는
[SIL Open Font License 1.1](resources/fonts/OFL.txt)을 따릅니다.

Copyright (C) 2026 Nyabi (nyattic)
