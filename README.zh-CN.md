<p align="center">
  <img src="docs/logo/logo.png" width="160" alt="ClaudeWeekUsageTray 标志">
</p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.ko.md">한국어</a> | <a href="README.ja.md">日本語</a> | <b>简体中文</b> | <a href="README.ru.md">Русский</a>
</p>

# ClaudeWeekUsageTray

一个小巧的 Windows 托盘程序，显示你的 Claude Code 订阅额度还剩多少。

通知区域里的数字是 **5 小时额度的剩余百分比**。点击它会打开面板，同时列出两个
额度窗口和各自的重置时间。

<p align="center">
  <img src="docs/tray-icon.png" width="72" alt="显示 73 的托盘图标"><br>
  <sub><em>托盘图标</em></sub>
  <br><br>
  <img src="docs/panel.png" width="320" alt="显示剩余 73% 和 59% 的详情面板"><br>
  <sub><em>详情面板</em></sub>
</p>

## 它做什么，以及绝不做什么

它显示用量。仅此而已。

它所持有的全部数据，就是 Claude Code 本来就会交给状态行命令的四个数字：5 小时
窗口和 7 天窗口的已用百分比与重置时间。

它 **不会**：

- 读取 `~/.claude/.credentials.json`、Windows 凭据管理器、浏览器存储或任何环境
  变量中的 API 密钥
- 读取或保存 OAuth 令牌、会话 ID 或对话记录
- 调用 `api.anthropic.com` 或任何其他网络服务
- 安装更新程序、注册开机自启动或发送遥测数据

没有登录流程，因为根本没有可登录的对象。登录由 Claude Code 负责，本程序只接收
Claude Code 主动送来的数字。完整的边界说明以及自行验证的方法见
[SECURITY.md](SECURITY.md)。

## 运行要求

- Windows 10 或 11，64 位
- 已安装并登录 Claude Code，并且它确实在运行

用量数据只有在 Claude Code 运行时才会送达。Claude Code 关闭期间没有任何东西会自动
刷新，本程序会如实说明这一点，而不是假装数据是新的。

## 安装

1. 下载发行版 ZIP，解压到你打算长期存放的位置，例如
   `C:\Tools\ClaudeWeekUsageTray`。里面只有两个文件：`ClaudeWeekUsageTray.exe`
   和 `uninstall.cmd`。
2. 用同一发行版中的 `SHA256SUMS-v*.txt` 校验下载：

   ```powershell
   Get-FileHash .\ClaudeWeekUsageTray.exe -Algorithm SHA256
   ```

3. 运行 `ClaudeWeekUsageTray.exe`。

   必须先告诉 Claude Code 发送用量，所以程序启动时会检查该设置是否就绪，没有的话
   就会询问：

   <img src="docs/first-run.png" width="404" alt="首次运行时询问是否连接 Claude Code 的窗口">

   选择 **Connect**，它会先备份你的设置文件，然后写入该设置。如果你已经在用自己的
   状态行命令，它会先说明，并且让那条命令继续运行。

4. 使用 Claude Code。状态行一被绘制——发一条消息就会发生——数字立刻出现。

在第一份数据到达之前，托盘显示 `--`。这是如实的状态，不是错误。

如果你更想自己完成设置，或者希望脚本化，有一条不弹窗、做同样事情的命令：

```powershell
.\ClaudeWeekUsageTray.exe --setup
```

Claude Code 会记住要运行的路径，所以移动文件夹会断开连接。在新位置启动程序，它会
察觉并提议把 Claude Code 指向新位置。把新版本解压到别处时也会出现同样的询问，
但要 **先用菜单里的 Exit 退出正在运行的那份**，否则新的那份不会弹窗，只会打开
正在运行那份的面板。

### 可执行文件没有代码签名

可执行文件未签名。首次运行时 Windows SmartScreen 会发出警告。如果你无法接受，
请用 `build.cmd` 自行从源码构建；构建只需要 Visual Studio 的 C++ 工具。

## 让图标显示出来

Windows 11 默认把新的托盘图标藏在折叠箭头后面。要固定它：

**设置 → 个性化 → 任务栏 → 其他系统托盘图标**，然后打开
**ClaudeWeekUsageTray**。

在此之前，图标位于点击时钟旁 `^` 后展开的列表中。

## 使用方法

| 操作 | 结果 |
| --- | --- |
| 左键点击图标 | 显示或隐藏详情面板 |
| 右键点击图标 | 菜单：**Show panel** 与 **Exit** |
| 再次运行程序 | 打开已在运行那份的面板 |
| 关闭面板 | 只隐藏面板，程序继续运行 |
| 菜单中的 **Exit** | 结束程序 |

面板会显示两个窗口的剩余百分比、各自的重置时间，以及数据最后一次送达的时间。
如果一段时间没有新数据，面板会直说，托盘数字也会变暗。陈旧的数值绝不会被当作
刚刚获取的数据来呈现。

## 如果你已经在用状态行

Claude Code 只允许一条 `statusLine` 命令，所以添加本质上就是覆盖。启动时的窗口会
说明这一点，并在动任何东西之前先询问；而 `--setup` 会直接拒绝，只告诉你它本来
打算做什么：

```powershell
.\ClaudeWeekUsageTray.exe --setup
```

要保留你的命令并在其之上加上托盘：

```powershell
.\ClaudeWeekUsageTray.exe --setup --wrap-existing
```

你的命令照常运行，收到完全相同的输入，输出也原样打印。程序只是额外把那四个数字
转发给托盘。原命令会被记录下来，以便随时还原。

两种情况下，写入之前 `settings.json` 都会备份为
`settings.json.cwut-backup-<时间戳>`，并且你原有的其他键（例如 `padding`）都会
保留。

撤销：

```powershell
.\ClaudeWeekUsageTray.exe --remove-statusline
```

如果你的 `statusLine` 条目是本程序无法识别的形式，它不会做任何改动，只会打印出
需要手动添加的内容。

## 清理重复的托盘条目

Windows 按可执行文件路径分别记住通知区域条目，因此移动或重新构建程序之后，设置
列表里可能残留旧条目。查看它们：

```powershell
.\ClaudeWeekUsageTray.exe --cleanup-tray-icons
```

删除残留的条目：

```powershell
.\ClaudeWeekUsageTray.exe --cleanup-tray-icons --apply
```

它只触碰 `HKEY_CURRENT_USER\Control Panel\NotifyIconSettings`，只处理可执行文件
为 `ClaudeWeekUsageTray.exe` 的条目，并且只在你自己的账户内操作。它会先写出
`.reg` 备份以便撤销，并且从不删除任何文件。

## 卸载

```powershell
.\uninstall.cmd
```

它会结束托盘图标、把状态行设置恢复原样，并在写出 `.reg` 备份后清理本程序的通知
区域条目。它不删除文件，所以之后请自行删除该文件夹；如果连备份也不想留，再删除
`%LOCALAPPDATA%\ClaudeWeekUsageTray`。

不会留下其他任何东西。没有安装程序，没有服务，除了任何托盘程序都会让 Windows
创建的那个通知区域条目之外，也没有其他注册表键。

## 从源码构建

```powershell
.\build.cmd
.\build\ClaudeWeekUsageTray.exe --self-test
pwsh -File .\tools\security-scan.ps1
```

需要 Visual Studio 2019 或更高版本，并勾选 **使用 C++ 的桌面开发**。此外没有任何
依赖：原生 Win32 与 C++17、静态 CRT、不用 .NET、不用第三方库。所有代码链接进一个
可执行文件。`build.cmd clean` 可清除输出。

生成发行版 ZIP 及其 `SHA256SUMS-v*.txt`：

```powershell
pwsh -File .\tools\package.ps1
```

## 工作原理

Claude Code 每次绘制界面都会运行它的 `statusLine` 命令，并把一段 JSON 数据通过
标准输入送进去。那条命令就是以 `ClaudeWeekUsageTray.exe --statusline` 启动的同一
个可执行文件。在该模式下，它从数据中只取出四个数字，通过环回 TCP 连接发给正在
运行的托盘。这条连接由一个 256 位令牌保护，令牌存放在只有你的账户能读取的文件中。
数据的其余部分不会被读取、保存或转发。

自己动手试的时候有一点要注意：PowerShell 不会等待 GUI 子系统的程序，所以
`echo '{...}' | .\ClaudeWeekUsageTray.exe --statusline` 会在输出到达之前就返回
提示符。Claude Code 读取管道的方式是正确的。想亲眼确认，请改用 `cmd /c` 运行。

这套机制是事件驱动的。只有当 Claude Code 送来数据时托盘才更新。托盘内部的 30 秒
计时器只是重绘它已经持有的数值，目的是让数字变暗、让措辞变成"陈旧"的时机准确，
它并不去取任何东西。

其余内容见 [DESIGN.md](DESIGN.md)。

## 许可证

MIT。见 [LICENSE](LICENSE)。
