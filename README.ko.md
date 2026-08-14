# ClaudeWeekUsageTray

Claude Code 구독 사용량이 얼마나 남았는지 보여주는 작은 Windows 트레이 프로그램입니다.

<img src="docs/tray-icon.png" width="72" alt="73을 표시하는 트레이 아이콘"> &nbsp;&nbsp;
<img src="docs/panel.png" width="300" alt="73%와 59% 잔량을 보여주는 상세 패널">

알림 영역의 숫자는 **5시간 한도의 남은 비율**입니다. 클릭하면 두 한도와 각각의
초기화 시각을 보여주는 패널이 열립니다.

다른 언어: [English](README.md)

## 하는 일과 하지 않는 일

사용량을 보여줍니다. 그게 전부입니다.

이 프로그램이 다루는 데이터는 Claude Code가 이미 상태 표시줄 명령에 넘겨주는
네 개의 숫자뿐입니다. 5시간 창과 7일 창의 사용 비율과 초기화 시각입니다.

다음은 **하지 않습니다**:

- `~/.claude/.credentials.json`, Windows 자격 증명 관리자, 브라우저 저장소,
  환경 변수 API 키를 읽지 않습니다
- OAuth 토큰, 세션 ID, 대화 기록을 읽거나 저장하지 않습니다
- `api.anthropic.com`을 비롯한 어떤 네트워크 서비스도 호출하지 않습니다
- 업데이터 설치, 시작 프로그램 등록, 텔레메트리 전송을 하지 않습니다

로그인 절차가 없습니다. 로그인할 대상이 없기 때문입니다. 로그인은 Claude Code가
담당하고, 이 프로그램은 Claude Code가 보내주는 숫자만 받습니다. 전체 경계와
직접 검증하는 방법은 [SECURITY.md](SECURITY.md)에 있습니다.

## 요구 사항

- Windows 10 또는 11, 64비트
- Claude Code 설치 및 로그인 완료, 그리고 실제로 실행 중일 것

사용량은 Claude Code가 실행 중일 때만 도착합니다. Claude Code가 꺼져 있으면
아무것도 스스로 갱신되지 않으며, 이 프로그램은 그 사실을 감추지 않고 그대로
표시합니다.

## 설치

1. 릴리스 ZIP을 계속 보관할 위치에 풀어 놓습니다. 예: `C:\Tools\ClaudeWeekUsageTray`.
   프로그램은 자신이 실행된 폴더 경로를 설정에 기록하므로, 나중에 폴더를 옮기면
   설정을 다시 실행해야 합니다.
2. `SHA256SUMS`로 파일을 확인합니다.

   ```powershell
   Get-FileHash .\ClaudeWeekUsageTray.exe -Algorithm SHA256
   ```

3. Claude Code에 연결합니다.

   ```powershell
   .\ClaudeWeekUsageTray.exe --setup
   ```

4. 트레이 프로그램을 실행합니다.

   ```powershell
   .\ClaudeWeekUsageTray.exe
   ```

5. Claude Code를 실행합니다. 첫 상태 표시줄 데이터가 도착하는 즉시 숫자가
   나타납니다.

첫 데이터가 도착하기 전까지 트레이는 `--`를 표시합니다. 오류가 아니라 사실
그대로의 상태입니다.

### 실행 파일에 코드 서명이 없습니다

두 실행 파일 모두 서명되어 있지 않습니다. 처음 실행할 때 Windows SmartScreen
경고가 나타납니다. 그것이 받아들이기 어렵다면 `build.cmd`로 직접 빌드하십시오.
Visual Studio의 C++ 도구 외에는 아무것도 필요하지 않습니다.

## 아이콘 보이게 하기

Windows 11은 새 트레이 아이콘을 기본적으로 꺾쇠 뒤에 숨깁니다. 고정하려면:

**설정 → 개인 설정 → 작업 표시줄 → 기타 시스템 트레이 아이콘**에서
**ClaudeWeekUsageTray**를 켭니다.

그 전까지는 시계 옆 `^` 꺾쇠를 눌렀을 때 열리는 목록 안에 있습니다.

## 사용법

| 동작 | 결과 |
| --- | --- |
| 아이콘 왼쪽 클릭 | 상세 패널 열기/닫기 |
| 아이콘 오른쪽 클릭 | **Show panel**, **Exit** 메뉴 |
| 프로그램 다시 실행 | 이미 실행 중인 창의 패널을 엽니다 |
| 패널 닫기 | 패널만 숨깁니다. 프로그램은 계속 실행됩니다 |
| 메뉴의 **Exit** | 프로그램을 종료합니다 |

패널은 두 한도의 남은 비율, 각각의 초기화 시각, 그리고 마지막으로 값이 도착한
시점을 보여줍니다. 한동안 아무것도 도착하지 않으면 그 사실을 분명히 표시하고
트레이 숫자가 흐려집니다. 오래된 값을 방금 받은 값처럼 보여주지 않습니다.

## 이미 상태 표시줄을 쓰고 있다면

Claude Code는 `statusLine` 명령을 하나만 허용합니다. 이미 설정된 명령이 있으면
`--setup`은 건드리지 않고, 무엇을 하려 했는지만 알려줍니다.

```powershell
.\ClaudeWeekUsageTray.exe --setup
```

기존 명령을 유지하면서 트레이를 함께 쓰려면:

```powershell
.\ClaudeWeekUsageTray.exe --setup --wrap-existing
```

기존 명령은 그대로 실행되고, 완전히 같은 입력을 받으며, 출력도 바뀌지 않고
그대로 인쇄됩니다. 헬퍼는 거기에 더해 네 개의 숫자만 트레이로 보냅니다. 원래
명령은 되돌릴 수 있도록 기록해 둡니다.

어느 경우든 `settings.json`은 쓰기 전에
`settings.json.cwut-backup-<시각>`으로 백업되며, `padding` 같은 다른 키도
그대로 보존됩니다.

되돌리려면:

```powershell
.\ClaudeWeekUsageTray.exe --remove-statusline
```

`statusLine` 항목이 인식할 수 없는 형태라면 아무것도 바꾸지 않고 직접 추가할
내용을 출력합니다.

## 중복된 트레이 항목 정리

Windows는 실행 파일 경로마다 알림 영역 항목을 따로 기억합니다. 그래서 프로그램을
옮기거나 다시 빌드하면 설정 목록에 오래된 항목이 남을 수 있습니다. 확인:

```powershell
.\cleanup-tray-icons.cmd
```

제거:

```powershell
.\cleanup-tray-icons.cmd --apply
```

이 명령은 `HKEY_CURRENT_USER\Control Panel\NotifyIconSettings`만, 실행 파일이
`ClaudeWeekUsageTray.exe`인 항목만, 그리고 현재 사용자 계정에서만 다룹니다.
먼저 `.reg` 백업을 남기므로 되돌릴 수 있고, 파일은 절대 삭제하지 않습니다.

## 제거

1. 트레이 메뉴에서 **Exit**.
2. `.\ClaudeWeekUsageTray.exe --remove-statusline`
3. `.\cleanup-tray-icons.cmd --apply`
4. 폴더를 삭제합니다. 백업까지 지우려면 `%LOCALAPPDATA%\ClaudeWeekUsageTray`도
   삭제합니다.

그 외에 남는 것은 없습니다. 설치 관리자도, 서비스도, 트레이 프로그램이라면
Windows가 으레 만드는 알림 영역 항목 외의 레지스트리 키도 없습니다.

## 소스에서 빌드

```powershell
.\build.cmd
.\build\ClaudeWeekUsageTray.exe --self-test
pwsh -File .\tools\security-scan.ps1
```

Visual Studio 2019 이상 + **C++를 사용한 데스크톱 개발**이 필요합니다. 그 외
의존성은 없습니다. 네이티브 Win32와 C++17, 정적 CRT, .NET 없음, 서드파티
라이브러리 없음. `build.cmd clean`으로 결과물을 지웁니다.

`SHA256SUMS`가 포함된 릴리스 ZIP을 만들려면:

```powershell
pwsh -File .\tools\package.ps1
```

## 동작 방식

Claude Code는 화면을 그릴 때마다 `statusLine` 명령을 실행하고 JSON 데이터를
표준 입력으로 넘깁니다. `ClaudeUsageStatusLine.exe`는 그 데이터에서 네 개의
숫자만 골라, 실행 중인 트레이에 루프백 TCP로 보냅니다. 이 연결은 현재 계정만
읽을 수 있는 파일에 저장된 256비트 토큰으로 보호됩니다. 데이터의 나머지 부분은
읽지도, 보관하지도, 전달하지도 않습니다.

이 구조는 이벤트 기반입니다. Claude Code가 무언가를 보낼 때만 갱신됩니다.
트레이 안의 30초 타이머는 이미 가지고 있는 값을 다시 그릴 뿐입니다. 숫자가
흐려지고 문구가 "오래됨"으로 바뀌는 시점을 맞추기 위한 것이고, 무언가를
가져오지 않습니다.

자세한 내용은 [DESIGN.md](DESIGN.md)에 있습니다.

## 라이선스

MIT. [LICENSE](LICENSE) 참고.
