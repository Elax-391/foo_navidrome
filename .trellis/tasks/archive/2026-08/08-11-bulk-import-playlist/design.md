# 全库一键导入技术设计

## 1. Architecture and Boundaries

本功能限制在 Windows 浏览器层，沿用现有三段边界：

```text
BrowserWindow 命令与状态
  -> SubsonicClientWin 顺序获取艺术家/专辑/歌曲
  -> BrowserWindow 主线程一次写入 foobar2000 播放列表
```

- `SubsonicClientWin` 的公开 API 和请求格式不变。
- `NavidromeNode` 继续作为树节点与播放列表元数据的共享模型。
- `enqueueNodes` 继续拥有 URI、metadb hint 和 playlist_manager 写入逻辑。
- 新功能只扩展根节点来源、任务编排、状态反馈和生命周期保护。

## 2. UI Changes

### 2.1 Command

- 在 `BrowserWindow` 增加 `IDC_ADD_ALL`、`m_addAllBtn` 和 `OnAddAll`。
- 按钮文本使用 `navidrome::l10n::addAllToPlaylist`，显示为“添加全部”。
- 动作行顺序为：刷新、状态区、添加全部、添加到播放列表、立即播放。
- 将现有三按钮的紧凑比例计算扩展为四按钮，继续对可用宽度、状态宽度和最后一个按钮宽度做非负保护。

### 2.2 Enablement

- 新增 `updateActionState()` 统一管理树、搜索、刷新、添加全部、添加和立即播放的启用状态。
- `m_libraryRoots` 非空且没有队列任务时才启用“添加全部”。
- 队列任务期间禁用树、搜索、刷新和三个播放列表动作入口；消息处理函数仍检查忙碌状态，防止快捷键或菜单绕过禁用状态。

## 3. Root State

- 保留 `m_rootNodes` 表示当前显示根节点。
- 新增 `m_libraryRoots` 表示最近一次成功加载的完整艺术家集合。
- `LoadedPayload` 增加明确的载荷来源标记（library/search）。只有 library 载荷可以更新 `m_libraryRoots`。
- `loadArtists()` 开始时清空 `m_libraryRoots` 并禁用“添加全部”；失败时保持禁用；成功时保存艺术家根并启用按钮。
- 搜索只更新显示根节点，不改变 `m_libraryRoots`；因此搜索状态下“添加全部”仍固定导入完整音乐库。

## 4. Shared Queue Operation

### 4.1 Entry Point

将 `queueSelected` 的核心逻辑抽成：

```cpp
void queueNodes(std::vector<std::shared_ptr<NavidromeNode>> roots,
                bool play,
                bool closeAfter,
                bool reportRootProgress);
```

- `queueSelected` 只负责取得当前选择并调用 `queueNodes`。
- `OnAddAll` 将 `m_libraryRoots` 的副本传给 `queueNodes(false, false, true)`。
- 所有入口首先检查 `m_queueInProgress`，确保单窗口只有一个队列任务。

### 4.2 Task State

新增由共享所有权管理的任务/窗口派发状态：

- 单调递增的 operation id，用于忽略过期进度或完成消息。
- `std::shared_ptr<std::atomic_bool>` 取消令牌。
- 不持有 `BrowserWindow*` 的窗口派发状态，包含 HWND 和主线程上的 alive 标记。
- UI 成员 `m_queueInProgress` 只在窗口线程读写。

后台线程只捕获根节点副本、操作参数、取消令牌和派发状态。进度与完成结果先通过 `fb2k::inMainThread` 回到主线程，再在 alive 且 operation id 匹配时发送给当前窗口；窗口销毁后直接释放载荷。

### 4.3 Collection Result

递归收集函数扩展为接受取消标记和统计对象：

```text
QueueResult
  songs: 成功获取的歌曲节点
  completedRoots: 已完成根艺术家数
  totalRoots: 根艺术家总数
  failedItems: 请求失败的艺术家或专辑数
  cancelled: 是否取消
```

- Song：直接加入结果。
- Album：已有 children 时复用；否则请求歌曲。请求失败时 `failedItems += 1`，继续调用方的下一个节点。
- Artist：已有 children 时复用；否则请求专辑；每张专辑继续按 Album 规则处理。
- 每完成一个全库根艺术家，派发一次进度，状态显示 `completedRoots/totalRoots`、歌曲数和当前失败数。
- 在每个网络调用之前和之后检查取消标记。无法中断正在执行的 WinHTTP 请求，但可阻止后续请求和播放列表写入。

## 5. Completion Contract

- 未取消且 `songs` 非空：在主线程调用现有 `enqueueNodes` 一次。
- `failedItems == 0`：沿用“已添加 N 首歌曲”。
- `failedItems > 0`：追加后覆盖为“已添加 N 首歌曲，M 个项目加载失败”。
- `songs` 为空：不调用播放列表写入；根据失败数显示无歌曲或全部加载失败。
- `closeAfter` 只影响原有 Enter 快捷键；全库按钮不自动播放、不关闭窗口。
- 完成或可见窗口内失败后清除忙碌状态并恢复控件。

## 6. Localization

在 `Windows/Localization.h` 集中增加：

- “添加全部”按钮文本。
- 已有任务正在进行提示。
- 全库进度格式化函数。
- 部分成功和全部失败汇总格式化函数。

不在 `BrowserWindow.cpp` 内拼接中文语法；服务器返回错误正文不翻译。

## 7. Compatibility and Migration

- 无配置、协议、数据库、URI 或安装目录迁移。
- 仅 Windows 源码和 Windows 工程既有文件发生变化，不新增第三方依赖。
- 原有单项添加、立即播放、右键菜单、Enter、搜索和刷新入口继续存在。
- 再次点击“添加全部”会再次追加完整库；不扫描已有播放列表。

## 8. Operational and Rollback Notes

- 代码回滚点 1：UI 按钮和布局。
- 代码回滚点 2：共享队列状态与递归错误统计。
- 代码回滚点 3：生命周期派发和取消。
- 构建树使用 `D:/FuShi-CStudy/foo_navidrome-build-cache/bulk-import-playlist-20260811/windows-x64-v145`，输出写入新的 `dist/zh-CN-win-x64-bulk-import/`，不覆盖 `dist/zh-CN-win-x64/`。
- 若运行时发现全库请求耗时不可接受，后续可单独研究并发限流或服务端批量端点；MVP 保持顺序请求，避免给 Navidrome 服务器造成突发并发压力。

## 9. Trade-offs

- 顺序请求耗时更长，但请求压力可预测，且不需要为 WinHTTP 客户端引入并发限流。
- 最后一次性追加需要在内存中保留歌曲节点，但能保证关闭/取消时不留下半成品。
- 没有显式取消按钮使 UI 更简单；用户仍可通过关闭窗口/偏好页终止未完成任务。
