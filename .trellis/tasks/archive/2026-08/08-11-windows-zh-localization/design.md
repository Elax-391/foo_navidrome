# Windows 中文本地化技术设计

## 1. 边界

本任务交付一个简体中文 Windows 组件，不增加运行时语言选择器，也不建立跨平台翻译系统。只改变 Windows 构建时的用户可见文本；macOS/Linux 行为保持原样。

## 2. 源码落位

实施开始后，将上游 `main` 分支导入工作区根目录并保留现有 `.trellis/`、`.agents/`、`.codex/`、`AGENTS.md` 与 Trellis `.gitattributes`。任务目录中的 `research/upstream/` 仅作为只读证据副本，所有产品改动必须发生在根目录源码中。

## 3. 字符串架构

新增仅供 Windows 编译使用的头文件 `Windows/Localization.h`：

- `namespace navidrome::l10n` 作为单一入口。
- Win32 控件使用 `inline constexpr wchar_t[]` 中文常量。
- foobar2000 API、状态栏与错误链使用 `inline constexpr char[]` UTF-8 常量。
- 艺术家数量、已添加曲目数量和错误前缀由小型内联格式化函数生成，避免在调用点拼接英文语序。
- `NavidromePluginWin.cpp`、`BrowserWindow.cpp`、`SubsonicClientWin.cpp` 引用同一组常量，按钮与右键菜单不得各自保留副本。

不采用 `.rc`/资源 DLL，是因为上游没有资源编译链；不增加 JSON/PO 运行时加载，是因为本任务只有一个目标语言，额外加载、回退和部署逻辑没有产品价值。

## 4. 编码

在 `Windows/foo_navidrome.vcxproj` 的公共 `ClCompile` 设置中增加 `/utf-8`，覆盖 Debug/Release 与 Win32/x64/ARM64EC。源码文件保存为 UTF-8，保证：

- `L"中文"` 被 MSVC 正确解析为宽字符；
- `u8"中文"`/UTF-8 `char` 文本可被 `MultiByteToWideChar(CP_UTF8, ...)` 和 foobar2000 字符串接口正确消费。

## 5. 翻译与保留策略

- 翻译：设置标签和按钮、连接状态、本地错误、浏览器标题/搜索/动作/状态、菜单命令和描述、缺失元数据兜底、Windows 组件 About 说明。
- 保留：品牌、协议、API、HTTP 状态码、错误码、请求头名称、服务器返回媒体内容和远端错误正文。
- `main.cpp` 的 About 文本使用 `_WIN32` 条件分支：Windows 为中文，其他平台继续使用上游英文，避免扩大平台行为改动。

## 6. 布局

- 浏览器底部动作区按中文按钮长度重新分配固定宽度，并确保状态栏宽度不会变为负值或与按钮重叠。
- 自定义请求头窗口增加提示文本高度，并为 Cloudflare 请求头按钮提供足够宽度。
- 设置页优先保持上游坐标结构，只在实际中文长度需要时扩大标签/按钮宽度，避免视觉重设计。

## 7. 验证

- 残留文本审计：按调研清单搜索 Windows 用户界面英文原文，确认无漏译或重复硬编码。
- 编码/工程审计：解析 `.vcxproj`，确认 `/utf-8` 和新头文件注册；检查源文件 UTF-8 可解码。
- 编译：优先执行 VS2022/MSBuild Release|x64；若 SDK 完整，再验证 Win32 与 ARM64EC。若环境缺少 MSBuild 或 SDK，准确记录缺失项和复现命令。
- 人工运行清单：独立浏览器、Media Library 内嵌浏览器、设置连接成功/失败、自定义请求头、File 菜单、右键菜单、搜索/加载/空选择/加入完成，以及 100%/125%/150% DPI。

## 8. 回滚

汉化集中在一个新头文件和少量调用点；若编码或构建失败，可先撤回 `/utf-8` 与中文常量引用，恢复上游字面量。配置 GUID、网络与播放逻辑不参与迁移，不需要数据回滚。
