# Windows 中文本地化实施计划

## 1. 导入上游源码

- [x] 在工作区根目录初始化/接入上游 Git `main`，保留现有 Trellis 与 Agent 管理文件。
- [x] 确认根目录产品源码与 `research/upstream/` 基准提交一致，后者保持只读。
- [x] 记录导入后的 `git status`，避免把研究克隆或生成物加入产品提交。

## 2. 建立 Windows 中文字符串入口

- [x] 新增 `Windows/Localization.h`，定义宽字符、UTF-8 字符串及动态数量/错误格式化函数。
- [x] 在 `Windows/foo_navidrome.vcxproj` 增加 `/utf-8` 并注册新头文件。
- [x] 保留 `Navidrome`、Subsonic/HTTP/WinHTTP/Cloudflare 及请求头名称。

## 3. 替换用户界面文本

- [x] 更新 `Windows/NavidromePluginWin.cpp`：设置页、自定义请求头窗口、连接状态、菜单名称/描述。
- [x] 更新 `Windows/BrowserWindow.cpp`：窗口标题、搜索提示、按钮、右键菜单、加载/错误/选择/搜索/加入状态和数量格式。
- [x] 更新 `Windows/SubsonicClientWin.cpp`：本地网络错误、无效响应、未知 Artist/Album/Title 兜底；远端错误正文继续透传。
- [x] 更新 `main.cpp`：仅 `_WIN32` 使用中文 About 说明，其他平台保留英文。

## 4. 调整 Windows 布局

- [x] 调整浏览器动作区按钮宽度和状态栏剩余宽度保护。
- [x] 调整自定义请求头提示区和 Cloudflare 按钮宽度。
- [x] 审查设置页中文标签与按钮是否适配现有宽度。

## 5. 验证

- [x] 审计已知英文 UI 原文与重复文案，确认仅保留技术名词、注释和非 Windows/非用户界面文本（FFF 服务中途不可用，使用 `git grep` 完成等价审计）。
- [x] 运行 XML/UTF-8/静态检查与 `git diff --check`。
- [x] 使用用户指定的 `reupen/foobar2000-sdk-unmodified`、隔离 SDK 布局、ATL/WTL 和全局 `v145` 覆盖完成 `Release|x64` 构建；0 警告、0 错误，详见 `verification.md`。
- [x] 对照 PRD 的人工运行清单记录已验证与受环境限制的项目。

## 6. 复核与交付

- [x] 运行 Trellis 质量检查，逐项核对 PRD 接受标准；检查代理超时后采用其已落盘修复并由主代理完成最终复核。
- [x] 审查最终 diff 仅包含产品源码、必要工程设置与 Trellis 任务记录。
- [x] 提供 Windows 构建/安装说明和未完成验证的明确边界。

## Rollback Points

- 上游导入后、汉化前记录干净基线。
- 字符串集中化与界面替换作为独立检查点。
- 构建设置只增加 `/utf-8`，若工具链不兼容可单独撤销并重新选择源码编码方案。
