# Verification

## Quality Gate

- `git diff --check`: passed（仅显示 Git 的 LF -> CRLF 工作区提示）。
- 修改文件严格 UTF-8 解码：passed。
- `Windows/foo_navidrome.vcxproj` XML 解析：passed；既有 `/utf-8` 与 `Localization.h` 注册未回归。
- 新增中文审计：按钮、进度、忙碌和失败汇总只定义在 `Windows/Localization.h`。
- 后台闭包审计：`BrowserWindow.cpp` 中加载、搜索、展开和队列后台闭包均不捕获裸 `this`。
- 两轮独立只读复核完成；首轮发现的“途中搜索被永久丢弃”和“展开载荷被丢弃”均已修正，针对性复核无阻塞问题。

## Acceptance Evidence

| AC | Evidence | Result |
| --- | --- | --- |
| AC1 | `OnAddAll` 直接复制 `m_libraryRoots`，不读取选择或展开状态。 | Static pass |
| AC2 | 全库根进入共享 `queueNodes`/`collectSongsDeep`，完成后只调用一次现有 `enqueueNodes`。 | Static + build pass |
| AC3 | 后台顺序请求；每完成一个根发送 processed/total、歌曲数、失败数进度。 | Static pass |
| AC4 | 控件禁用、所有动作入口忙碌保护、operation id 过滤迟到消息。 | Static pass |
| AC5 | 艺术家/专辑错误累计后继续；零歌曲不调用播放列表写入。 | Static pass |
| AC6 | 共享 dispatch alive 状态、取消令牌、窗口销毁失活；取消结果不追加。 | Static pass |
| AC7 | 单项/播放/双击/Enter/右键复用共享队列；搜索结果独立于 `m_libraryRoots`，展开载荷在队列结束后延迟应用。 | Static + independent review pass |
| AC8 | `Release|x64` clean rebuild succeeded；DLL/package architecture and integrity checks passed. | Build pass |

## Windows x64 Clean Rebuild

- SDK: `https://github.com/reupen/foobar2000-sdk-unmodified`
- Build tree: `D:/FuShi-CStudy/foo_navidrome-build-cache/bulk-import-playlist-20260811/windows-x64-v145`
- Toolset: Visual Studio Build Tools 2026, `v145`, ATL, vendored WTL.
- Command: `MSBuild ... /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145`.
- Result: 0 errors. Eight warnings were emitted only by upstream pfc/SDK/helpers/shared deprecated-API code; the product project emitted no warning.
- PE machine: `0x8664` (x64).
- DLL size: 251,904 bytes.
- DLL SHA-256: `A59E58141F18D16B5F1C976CA2D3274AE4C9F027155F968CC63602A4734A2337`.
- DLL contains UTF-16LE “添加全部” and UTF-8 “正在导入：已处理” markers.

## Package Integrity

- Package: `dist/zh-CN-win-x64-bulk-import/foo_navidrome_1.3.0_zh-CN_bulk-import_win-x64.fb2k-component`
- Package size: 111,429 bytes.
- Package SHA-256: `80D8DAB5AD1E09963B1844239D0B1C3AA4B3D4EB26DF695558A08197B13F24B9`.
- ZIP integrity/CRC: passed.
- Entries: exactly `x64/foo_navidrome.dll`.
- Packaged DLL SHA-256 equals the standalone DLL hash.

## Runtime Boundary

This environment did not connect the component to the user's live Navidrome server or install it into the active foobar2000 profile. The following remain recommended manual smoke checks:

- Click “添加全部” with no selection/expansion and observe progress through the real library.
- Confirm partial API failure reporting if a request can be induced safely.
- Start an import while a search/expand request is in flight.
- Close the preferences page during import and confirm no partial playlist append.
- Inspect standalone/embedded layouts at 100%, 125%, and 150% DPI.
