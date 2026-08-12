# Navidrome 增量枚举与本地状态研究

## 结论

OpenSubsonic 1.16.1 没有“列出某时刻以后新增歌曲”的标准端点或歌曲级变更游标。要同时做到不漏歌和不重复添加，有两种可实现路径：

1. **严格模式**：每次通过 `search3` 空查询分页枚举全部歌曲 ID，再与本地精确 ID 集合比较。它仍然扫描全部歌曲元数据，但请求数量会从“艺术家 + 专辑逐级请求”降为按歌曲大页分页，通常会快很多。
2. **Navidrome 快速增量模式**：首次完整分页并保存精确 ID 集合、总数和尾部锚点；后续从旧尾部附近继续分页，只处理锚点之后的歌曲。Navidrome 当前实现对空查询使用 `media_file.rowid` 自然顺序，因此新数据库行通常追加在尾部。若锚点、服务器、用户或可访问音乐库发生变化，则自动退回严格完整核对。

快速增量依赖 Navidrome 当前实现的稳定自然顺序，而不是 OpenSubsonic 协议保证。因此设计必须保留完整核对回退和手动重建入口。

## 官方 API 证据

Navidrome 实现证据固定到源码提交 `9e95b19a4faa325d106630a7c19cd71f31db43b1`，避免后续主分支变化使研究结论失去版本边界。

### `getIndexes(ifModifiedSince)` 不是歌曲增量接口

- OpenSubsonic 文档只承诺：当“艺术家集合”在给定时间后发生变化时返回结果。
- Navidrome 当前实现将 `ifModifiedSince` 与最后一次扫描开始时间比较，然后返回或省略完整艺术家索引；它不返回变更歌曲列表。
- 证据：
  - https://opensubsonic.netlify.app/docs/endpoints/getindexes
  - https://github.com/navidrome/navidrome/blob/9e95b19a4faa325d106630a7c19cd71f31db43b1/server/subsonic/browsing.go#L33-L112

### `getAlbumList2(type=newest)` 不能保证不漏旧专辑的新歌

- 端点返回专辑列表，`newest` 的排序对象是专辑；单页最大 500 张专辑。
- 它没有歌曲级 `since` 参数。向既有专辑加入歌曲不一定会使该专辑可靠地出现在可覆盖范围内，因此不能作为唯一增量来源。
- 证据：
  - https://opensubsonic.netlify.app/docs/endpoints/getalbumlist2
  - https://github.com/navidrome/navidrome/blob/9e95b19a4faa325d106630a7c19cd71f31db43b1/server/subsonic/album_lists.go#L19-L118

### `search3` 可以分页枚举歌曲，但协议不规定自然顺序

- OpenSubsonic 文档允许空查询，并提供 `songCount` / `songOffset` 分页参数；返回歌曲对象包含 `created`。
- Navidrome 当前测试明确验证空查询返回全部实体。
- Navidrome 当前实现把空查询歌曲按 `media_file.rowid` 排序，再应用 offset/limit；这给尾部增量提供了实际基础。
- `search3` 没有按 `created` 过滤或排序的标准参数，所以不能只凭时间水位线向服务器请求增量。
- 证据：
  - https://opensubsonic.netlify.app/docs/endpoints/search3
  - https://github.com/navidrome/navidrome/blob/9e95b19a4faa325d106630a7c19cd71f31db43b1/server/subsonic/searching.go#L22-L152
  - https://github.com/navidrome/navidrome/blob/9e95b19a4faa325d106630a7c19cd71f31db43b1/server/subsonic/e2e/subsonic_searching_test.go#L101-L121
  - https://github.com/navidrome/navidrome/blob/9e95b19a4faa325d106630a7c19cd71f31db43b1/persistence/sql_search.go#L50-L128
  - https://github.com/navidrome/navidrome/blob/9e95b19a4faa325d106630a7c19cd71f31db43b1/persistence/mediafile_repository.go#L514-L532

## 本地实现边界

### HTTP 并发

`SubsonicClientWin::httpGet()` 每次调用独立创建并关闭 WinHTTP session、connection 和 request 句柄，没有共享请求句柄或可变响应缓冲区。因此客户端方法可用于有界并发。并发数仍应固定在较小值，并在任务取消后停止派发新请求。

证据：`Windows/SubsonicClientWin.cpp:241-299`。

### 状态持久化

不建议把大型歌曲 ID 集合塞进 `cfg_string`。建议在 foobar2000 profile 下维护一个组件专用状态文件：

- 按规范化服务器 URL + 用户名隔离，不保存密码或认证 token；
- 保存格式版本、可访问音乐库指纹、精确歌曲 ID 集合、已完成歌曲数和尾部锚点；
- 先写临时文件，再原子替换正式文件；
- 只有完整枚举成功且未取消时，才同时提交播放列表追加和新状态；
- 状态损坏、锚点失配、音乐库访问范围变化或 API 不兼容时，自动进行完整核对；
- 提供“重建导入基线”入口。

foobar2000 SDK 提供 `core_api::pathInProfile()` 和 `filesystem::g_get_native_path()`，可以得到 profile 内的组件文件路径。

### 兼容回退

若服务器不支持 `search3` 空查询分页，则保留现有艺术家→专辑→歌曲递归作为兼容回退，并为该回退增加固定上限并发。回退仍用精确 ID 集合过滤重复歌曲，但无法实现真正的尾部增量。

### 扫描一致性与多窗口边界

- Navidrome 官方兼容性文档说明 `getScanStatus` 额外返回 `lastScan`；实现以分页前后的 `scanning + lastScan` 作为一致性 token，并用相邻页 1 项重叠检测 offset 漂移。
- `BrowserWindow` 同时存在 standalone singleton 和偏好页内嵌实例，实例级 `m_queueInProgress` 无法互斥，因此全库导入需要按服务器 + 用户建立进程级 RAII lease。
- 播放列表和状态文件不能组成真正跨系统事务；正式状态替换失败时必须凭追加 receipt 同步删除本次范围，窗口丢弃载荷时由 RAII 清理临时文件。
- 官方兼容性证据：https://www.navidrome.org/docs/developers/subsonic-api

## 用户决定

用户选择“Navidrome 快速增量 + 检测异常时自动完整核对”。实现必须明确标注该能力依赖 Navidrome 当前自然顺序，并保留精确 ID 集合、尾部锚点、身份/版本/音乐库/扫描 token 校验、分页重叠验证、完整核对和递归兼容回退。
