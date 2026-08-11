# 全库一键导入实施计划

## 1. 准备与基线

- [x] 运行 Trellis 开发前上下文，确认任务状态、适用 spec、工作树和既有用户改动。
- [x] 记录当前 `Windows/BrowserWindow.h`、`Windows/BrowserWindow.cpp`、`Windows/Localization.h` 基线及上一版汉化构建散列。
- [x] 确认隔离 SDK/ATL/WTL 构建缓存仍可用，旧 `dist/zh-CN-win-x64/` 保持只读。

## 2. 增加完整艺术家根状态和 UI

- [x] 在 `LoadedPayload` 增加载荷来源，在 `BrowserWindow` 增加 `m_libraryRoots`。
- [x] 仅完整艺术家成功载荷更新 `m_libraryRoots`；搜索载荷不得覆盖。
- [x] 增加 `IDC_ADD_ALL`、`m_addAllBtn`、`OnAddAll` 和统一动作状态更新函数。
- [x] 调整四按钮动作行布局，验证紧凑宽度计算不为负。
- [x] 在 `Windows/Localization.h` 增加集中化按钮和状态文本。

## 3. 重构共享队列操作

- [x] 将 `queueSelected` 重构为选择适配层，并新增接收多个根节点的 `queueNodes`。
- [x] 为单项添加、立即播放、Enter、右键菜单、双击和“添加全部”建立一致的忙碌保护。
- [x] 新增 operation id、取消令牌、窗口派发状态和进度/完成消息。
- [x] 后台线程不得捕获裸 `this`；`OnDestroy` 标记窗口失效并请求取消。

## 4. 扩展递归统计和错误处理

- [x] 让递归收集返回歌曲、失败项和取消状态，保留服务器顺序。
- [x] 艺术家/专辑请求失败时继续其余节点，不吞掉失败计数。
- [x] 每完成一个全库艺术家报告一次进度。
- [x] 完成后只调用一次现有播放列表写入；取消或零歌曲不得修改播放列表。
- [x] 部分成功、全部失败和正常完成状态使用本地化格式化函数。

## 5. 静态验证

- [x] 用 FFF（不可用时记录并回退 `git grep`）确认新增中文按钮/状态没有散落在 `Localization.h` 外。
- [x] 严格 UTF-8 解码所有修改的 C++/头文件。
- [x] 解析 `Windows/foo_navidrome.vcxproj` XML，确认既有 `/utf-8` 和文件注册未回归。
- [x] 运行 `git diff --check`。
- [x] 人工审查所有队列入口都经过忙碌保护，后台闭包没有捕获裸 `this`。

建议命令：

```powershell
git diff --check
git grep -n -E "添加全部|正在导入|加载失败" -- Windows
```

## 6. Windows x64 构建与打包

- [x] 将本任务修改的产品文件复制到隔离构建树的 `workspace/foo_navidrome`，不修改 SDK 镜像。
- [x] 复用 `v145`、ATL、WTL 和 `reupen/foobar2000-sdk-unmodified` 布局运行 `Release|x64`。
- [x] 验证 MSBuild 0 错误、PE machine 为 x64、DLL 包含新增中文标记。
- [x] 在 `dist/zh-CN-win-x64-bulk-import/` 生成新 DLL 和 `.fb2k-component`，校验 ZIP 仅含 `x64/foo_navidrome.dll` 且内部 DLL 散列一致。

构建命令基线：

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\FuShi-CStudy\foo_navidrome-build-cache\bulk-import-playlist-20260811\windows-x64-v145\workspace\foo_navidrome\Windows\foo_navidrome.vcxproj' `
  /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 `
  /p:SolutionDir='D:\FuShi-CStudy\foo_navidrome-build-cache\bulk-import-playlist-20260811\windows-x64-v145\workspace\foo_navidrome\' `
  /p:OutDir='D:\FuShi-CStudy\foo_navidrome-build-cache\bulk-import-playlist-20260811\windows-x64-v145\out\x64\' `
  /m /nologo /v:minimal
```

实际最终验证在同一命令上增加 `/t:Rebuild`，确保所有产品和 SDK 项目从干净目标重新编译。

## 7. 行为复核

- [x] 无选择、无展开时“添加全部”可启动完整库任务。
- [x] 搜索状态下“添加全部”仍使用完整艺术家根集合。
- [x] 进度显示 processed/total、歌曲数和失败数；重复入口被拒绝。
- [x] 单个请求失败继续，其余成功歌曲只在结束时一次性追加。
- [x] 关闭窗口后不追加、不触碰失效窗口。
- [x] 原有单项添加、立即播放、Enter、右键菜单、搜索和刷新不回归。
- [x] 若当前环境不能自动运行 foobar2000，明确把真实服务器全库烟测列为交付后的人工验证项。

## 8. 交付与回滚

- [x] 运行 Trellis check，按 PRD 的 AC1-AC8 逐项核对。
- [x] 审查最终 diff 只包含本任务产品源码、任务文档和必要验证记录。
- [x] 保留旧汉化 DLL/安装包，并报告新产物绝对路径、大小、SHA-256 和运行时未验证边界。
