# Windows 中文本地化

## Goal

将 `santiagorod92/foo_navidrome` 的 foobar2000 Windows 组件用户界面汉化为简体中文，让中文用户能够在 Windows 上完成配置、连接 Navidrome、浏览音乐、搜索、播放和加入播放列表。

## Background and Confirmed Facts

- 上游仓库：`https://github.com/santiagorod92/foo_navidrome`，当前主分支提交已暂存于本任务 `research/upstream/`，产品源码尚未写入工作区根目录。
- 项目是 foobar2000 原生 C++ 组件；Windows 工程为 `Windows/foo_navidrome.vcxproj`，使用 C++17、Win32/ATL/WTL 和 Unicode 宽字符控件（`Windows/foo_navidrome.vcxproj:92-112`）。
- Windows 自有 UI 没有 `.rc`、STRINGTABLE、资源 DLL 或既有 i18n/locale 装载链；偏好页和浏览器窗口通过代码创建，英文文案直接散落在 `Windows/NavidromePluginWin.cpp`、`Windows/BrowserWindow.cpp` 和 `Windows/SubsonicClientWin.cpp`（`Windows/NavidromePluginWin.cpp:177`；`Windows/foo_navidrome.vcxproj:146-163`）。
- Windows 构建依赖 VS2022/MSBuild v143；CI 构建 Win32、x64、ARM64EC 并打包 `.fb2k-component`（`README.md:121-145`；`.github/workflows/build-windows.yml:46-52,147-223`）。
- 可控的用户界面文案包括：设置页标签/按钮/状态、自定义请求头窗口、File 菜单命令、浏览器标题/搜索/按钮/右键菜单/加载与选择状态、组件自身的网络错误与缺省 Artist/Album 文案（证据见 `Windows/NavidromePluginWin.cpp:61-115,227-288,372-400`、`Windows/BrowserWindow.cpp:40,71,81-90,135,173-178,265,348-350,399-417,490,543-548`、`Windows/SubsonicClientWin.cpp:176-182,250-291,318-339`）。
- 品牌名 `Navidrome`、协议/API 名、HTTP 状态码、Cloudflare 请求头名称、配置键、文件路径、歌曲/艺术家等服务端返回内容不属于翻译对象；服务端返回的错误消息只翻译本地前缀或兜底文本。

## Requirements

- 仅修改 Windows 用户界面源码及为集中管理这些界面文案所需的 Windows 专属辅助代码；不修改 macOS/Linux UI，不翻译 `scripts/*.sh` 的开发者命令行提示。
- 优先建立 Windows 专属的集中字符串入口，避免同一文案在按钮、右键菜单、状态栏等位置重复硬编码；不得改变配置 GUID、配置键、网络协议、元数据键或播放逻辑。
- 覆盖设置/配置流程：Server URL、Username、Password、Test Connection、Testing、Connected、失败状态、Custom Headers 窗口标题/说明/Cloudflare headers/Save/Cancel。
- 覆盖浏览与播放流程：窗口标题、搜索提示、Add/Play/Refresh、Loading/Search/选择为空/无歌曲/加入完成、艺术家数量和错误提示，以及右键菜单的重复文案。
- 覆盖 File 菜单命令名称和描述；保留 `Navidrome` 品牌名。
- 覆盖 Windows 组件信息页中的功能说明和配置/浏览路径；通过 `_WIN32` 条件分支保持其他平台原有英文说明。
- 对中文文本可能引起的固定控件宽度问题进行代码级调整或验证，至少检查 100%/125%/150% DPI 与窄窗口布局。

## Acceptance Criteria

- [ ] Windows 端上述主要用户流程中的自有英文 UI 文案均已替换为简体中文；服务端返回内容和明确保留的技术名词除外。
- [ ] 相同语义的按钮、右键菜单、状态栏和错误前缀使用一致中文措辞，不存在只改一处的重复英文。
- [ ] 中文源码在 Unicode 编译设置下可正常显示，无乱码；控件无明显截断、重叠或不可操作。
- [ ] Windows 工程至少通过静态检查/编译验证；若当前环境缺少 VS2022/foobar2000 SDK，则记录未完成的运行验证及可复现命令。
- [ ] 改动不改变配置格式、跨平台 GUID、网络请求、播放控制、服务端元数据和 Navidrome 组件安装方式。
- [ ] 工作区根目录包含可继续构建的上游源码与汉化改动，且 Trellis 任务研究副本不被误当作交付源码。

## Out of Scope

- Linux 与 macOS 的界面适配、翻译资源和打包验证。
- Navidrome 服务端/Web UI、服务端返回的歌曲/艺术家/错误消息本地化。
- `scripts/*.sh`、CI workflow、发布说明等开发者命令行文本的翻译。
- 与中文化无关的新功能、视觉重设计、架构重构或配置迁移。
