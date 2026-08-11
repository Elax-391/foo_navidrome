# Windows 全库导入研究结论

## Current UI and Selection Model

- `Windows/BrowserWindow.cpp:74-79` 创建标准 `CTreeViewCtrl`，样式中没有可靠的原生多选支持。
- `Windows/BrowserWindow.cpp:348-361` 的 `selectedNodes()` 只扫描 `TVIS_SELECTED`；现有 UI 的双击、Enter 和右键目标也都是单个 `GetSelectedItem()`。
- 因此最小且稳定的产品入口是独立“添加全部”命令，而不是维护自定义选择集合、锚点、绘制、鼠标和键盘语义。

## Root Data Ownership

- `Windows/BrowserWindow.h:116-118` 只保存 `m_rootNodes`。
- `Windows/BrowserWindow.cpp:155-175` 加载完整艺术家；`Windows/BrowserWindow.cpp:430-459` 将搜索歌曲也发送到同一个根载荷处理函数。
- `Windows/BrowserWindow.cpp:192-200` 会用最近载荷覆盖 `m_rootNodes`，所以批量入口必须另存完整艺术家集合；否则搜索后“添加全部”会错误地只处理搜索歌曲。

## Recursive Fetch and Playlist Boundary

- `Windows/BrowserWindow.cpp:465-504` 已实现 Song/Album/Artist 的同步递归收集，适合在后台线程复用。
- `Windows/SubsonicClientWin.h:17-20` 仅暴露 artists、artist albums、album songs 和 search；完整库需要大量顺序 HTTP 请求。
- 当前递归忽略 `outError`，全库任务必须显式累计失败艺术家/专辑，并继续其余节点。
- `Windows/BrowserWindow.cpp:509-564` 是播放列表写入的唯一契约所有者：生成 `navidrome://track/...`、写入元数据提示、必要时创建活动播放列表并一次追加条目。

## Concurrency and Lifetime Risk

- `Windows/BrowserWindow.cpp:367-380` 的队列线程捕获裸 `this`；全库操作持续时间显著更长，会放大嵌入偏好设置页销毁后的失效访问风险。
- 新的共享队列路径应捕获根节点副本、取消令牌和窗口派发状态，不捕获裸窗口对象。
- UI 回调通过 foobar2000 主线程派发；派发状态在 `OnDestroy` 标记失效，从而安全丢弃进度和完成载荷。

## Build Baseline

- 已验证 SDK 来源为 `https://github.com/reupen/foobar2000-sdk-unmodified`。
- `.trellis/tasks/archive/2026-08/08-11-windows-zh-localization/research/sdk-build.md` 记录了成功的 `Release|x64`、`v145`、ATL/WTL 和 SDK 镜像布局。
- 上一任务缓存移动后，其 junction 仍指向已归档前的旧任务路径，不能直接复用。为避免修改旧缓存，本任务新建 `D:/FuShi-CStudy/foo_navidrome-build-cache/bulk-import-playlist-20260811/windows-x64-v145`，并把 SDK、helpers、shared、component client、pfc、libPPUI junction 直接指向仍有效的 SDK 镜像。
- 本任务只更新新隔离树中的产品副本并输出到新的分发目录，不覆盖上一版汉化产物。
