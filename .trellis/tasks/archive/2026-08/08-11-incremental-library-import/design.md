# 全库并发与增量导入技术设计

## 1. Architecture and Boundaries

本功能保持现有 UI、网络和播放列表边界，同时新增一个 Windows 专用导入状态层：

```text
BrowserWindow 命令与任务生命周期
  -> SubsonicClientWin 配置快照 + 分页/兼容 API
  -> LibraryImportState 精确 ID、游标与锚点持久化
  -> BrowserWindow 主线程一次追加播放列表并提交状态
```

- `SubsonicClientWin` 负责认证、HTTP 和 Subsonic JSON 到类型的转换，不读写状态文件。
- `LibraryImportState` 负责状态格式、校验、临时文件和原子替换，不调用网络或播放列表 API。
- `BrowserWindow` 负责选择快速增量、完整核对或递归回退，并维持窗口生命周期与主线程写入边界。
- `enqueueNodes()` 继续唯一拥有 `navidrome://track/...` URI、metadb hint 和 `playlist_manager` 写入逻辑。

## 2. Request Snapshot Contract

新增不可变 `SubsonicRequestContext`：

```text
serverUrl
username
password
salt
customHeaders
```

- UI 线程在任务开始时从现有 `cfg_string` 捕获一次。
- 长任务中的所有 ping、音乐库和歌曲分页请求都使用同一快照。
- 密码、salt 和请求头只存在内存中，不进入状态文件。
- 现有普通浏览方法可继续由便捷重载捕获当前配置；导入路径必须显式传递快照。

## 3. Subsonic Enumeration API

### 3.1 Server and Library Identity

增加：

- `getServerInfo(context)`：调用 `ping.view`，读取 `type`、`serverVersion` 和 OpenSubsonic 标记。
- `getMusicFolders(context)`：调用 `getMusicFolders.view`，读取当前用户可访问的 folder ID。
- `getScanStatus(context)`：读取 `scanning` 和 Navidrome 扩展的 `lastScan`，作为本轮分页的一致性 token。
- folder ID 排序后生成音乐库指纹；名称变化不使歌曲范围失效。

快速尾部模式只对识别为 Navidrome 且支持空查询的服务器启用。其他兼容服务器使用严格分页完整核对；空查询完全不支持时再进入递归回退。

### 3.2 Song Pages

增加 `getSongsPage(context, offset, count, outError)`，请求参数为：

```text
query=""
artistCount=0
albumCount=0
songCount=500
songOffset=<offset>
```

- 页大小常量为 500，后续页从前页最后一项开始形成 1 项重叠，实际每页推进 499 首。
- 解析完整播放所需字段，并保留 `created` 作为诊断信息，不把它当作服务端游标。
- 第一页以后必须以预期重叠 ID 开头；该 ID 只消费一次。空 ID、页内重复、非预期跨页重复、边界失配、连续页面不前进和错误响应都视为一致性失败，不能静默去重后提交。
- 页数未知，因此按 offset 顺序请求，直到短页或空页；不并发猜测不存在的总页数。
- 开始时若 `scanning=true` 直接提示稍后重试。完成后再次读取 scan status，要求 `scanning=false` 且 `lastScan` 与开始相同；否则丢弃结果并从头重试一次，第二次仍不稳定则失败。

## 4. Persistent State Contract

新增 `Windows/LibraryImportState.h/.cpp`，采用版本化二进制格式：

```text
magic + formatVersion
fullIdentity              # 规范化 server URL + username
serverType + serverVersion
libraryFingerprint
cursorCount               # 上次确认的当前自然顺序长度
knownSongIds              # 精确集合
tailAnchors               # 最后最多 64 个有序 ID
```

- 文件名使用配置身份的稳定散列，文件内容仍保存完整非敏感身份以检测散列碰撞。
- 每个字符串和集合都有长度上限；magic、版本、截断、重复或超限数据均判为无效。
- `knownSongIds` 在内存中转换为 `std::unordered_set` 做精确差集。
- 完整核对成功后，用当前完整 ID 集合替换旧集合。
- 快速增量成功后，将新 ID 并入精确集合，并根据锚点的实际位置更新 `cursorCount` 和尾部锚点；历史已删除 ID 可保留到下一次完整核对。
- 状态文件位于 foobar2000 profile，使用 `core_api::pathInProfile()` 和 `filesystem::g_get_native_path()` 获得本机路径。
- 加载函数另外返回正式文件完整字节的 generation 摘要，该摘要不写入文件本身；提交前重新计算，与加载值不同则拒绝覆盖并进入补偿回滚。

### 4.1 Compensating Commit Protocol

1. 后台完成枚举和差集。
2. 后台把新状态写入 operation 专用临时文件，并 flush/close 成功。
3. 完成载荷携带 move-only `PreparedStateFile` 回到主线程；其析构函数在未提交时删除临时文件。
4. 主线程一次追加所有新增歌曲，并取得 `PlaylistAppendReceipt { playlist, insertPos, count }`；追加 API 返回失败则不提交状态。
5. 校验正式状态 generation 后，用 `MoveFileEx(..., REPLACE_EXISTING | WRITE_THROUGH)` 原子替换。
6. 正式替换失败或 generation 冲突时，立即调用 `playlist_remove_items(playlist, bit_array_range(insertPos, count))` 并核对项目数恢复；旧状态保持不变。
7. 正式替换成功后 disarm 临时文件守卫；普通成功、零新增、失败、取消和窗口销毁都通过 RAII 清理剩余资源。

正常错误路径由补偿删除保证“播放列表追加”和“状态前移”最终同时可见或同时不可见。两个存储系统无法覆盖进程在步骤 4 与 5 之间被强制终止的极小崩溃窗口，该限制在 PRD 明示。

### 4.2 Identity-Scoped Import Lease

- 新增进程级导入协调器，以规范化 server URL + username 为 key 维护活动 lease。
- 独立浏览器 singleton 与嵌入偏好页实例在全库导入前都必须取得同一协调器 lease。
- 同身份第二个任务立即返回“该账户已有导入任务”；不同身份可并行，状态文件彼此隔离。
- lease 由 move-only/共享 RAII 对象持有，随后台任务、完成载荷或丢弃载荷释放，不能只依赖某个窗口的 `m_queueInProgress`。

## 5. Import Mode State Machine

### 5.1 Automatic `添加全部`

```text
捕获配置 + 取得身份 lease + 读取状态 + 获取服务器/音乐库/扫描身份
  -> 无状态：完整分页导入
  -> 非 Navidrome / 不支持快速能力：完整分页核对
  -> 身份、版本、音乐库或格式不匹配：完整分页核对
  -> 有效 Navidrome 基线：快速尾部增量
       -> 锚点有效：仅继续尾部
       -> 锚点无效：完整分页核对
  -> 空查询不支持：有界并发递归回退
```

### 5.2 Fast Tail Algorithm

- 从 `max(0, cursorCount - 500)` 开始取页。
- 在返回序列中寻找保存的尾部锚点，并验证多个可用锚点仍保持相同相对顺序；小曲库按实际可用锚点数验证。
- 若锚点因删除向前偏移，则每次再向前回退一个 500 首页面，直到找到或到达 offset 0。
- 找到最后一个可信锚点后，只把它之后且不在 `knownSongIds` 的歌曲作为新增候选，并继续分页到短页/空页。
- 找不到可信锚点、顺序不一致或分页不前进时，不猜测游标，立即切换完整核对。
- 快速分页同样使用 1 项页边界重叠，并在完成后复核 scan token；token 变化时丢弃结果并重试一次。

### 5.3 Full Reconciliation

- 从 offset 0 分页到结束。
- 每页验证预期重叠项；任何其他跨页重复或边界错位都把本轮标记为不稳定。
- 建立当前完整 ID 集合和新的尾部锚点。
- 仅将旧精确集合中不存在的 ID 作为候选。
- 首次没有旧状态时，所有有效歌曲均为候选。
- 用户点击新增的“完整核对”按钮时强制走此路径。

### 5.4 Recursive Compatibility Fallback

- 使用同一请求快照重新调用 `getArtists(context)` 获取根，不复用 UI 先前加载的 `m_libraryRoots`。
- `collectSongsDeep` 显式接收并贯穿同一 context 到艺术家、专辑和歌曲请求。
- 固定 4 个 worker，通过原子根索引领取艺术家；每个 worker 使用局部歌曲/失败结果，合并时加锁。
- 每完成一个根艺术家发送进度；取消后不再领取新根。
- 回退结果仍与精确 ID 集合做差集并遵守两阶段提交，但因为协议能力不足，后续运行仍可能需要递归全库。

## 6. UI and Payload Changes

- 保留“添加全部”，其行为改为自动模式状态机。
- 在动作行增加“完整核对”按钮；导入期间与其余控件一起禁用。
- 五按钮布局继续使用比例压缩和非负宽度保护；窄窗口可以隐藏状态区，但不能产生负尺寸。
- `QueueCompletePayload` 增加操作类型、候选歌曲、move-only 临时状态守卫、身份 lease、模式、扫描数量和错误状态；不把裸路径当作资源所有权。
- 增量无新歌时仍提交经过验证的新游标/状态，并显示“没有新增歌曲，音乐库已是最新”。
- 新增中文状态包括：检查新增、完整核对、分页进度、自动回退、已是最新、状态提交失败和基线重建完成；全部集中在 `Localization.h`。

## 7. Failure and Cancellation Semantics

- 网络或解析错误：不追加、不提交，保留旧状态并显示本地化前缀 + 原始远程错误。
- scan token 变化、页内空 ID、非预期跨页重复、重叠边界失配或分页无进展：丢弃本轮；最多完整重试一次，仍异常则终止。
- 状态损坏：忽略该状态并完整导入/核对，不覆盖其他服务器或用户状态。
- 正式状态提交失败或 generation 冲突：同步回滚 `PlaylistAppendReceipt` 指定的追加范围，报告失败，保留旧状态。
- 窗口关闭：设置取消标记、使 operation id 失效；载荷析构自动删除临时文件并释放身份 lease。
- 正在执行的同步 WinHTTP 请求不能即时中断，但请求返回后必须停止后续工作和播放列表写入。
- 配置在任务期间变化：当前任务继续使用快照；下一次任务使用新配置和独立状态。

## 8. Compatibility and Migration

- 已有用户第一次运行新版时没有状态文件，执行一次分页完整导入并建立基线。
- 不修改旧 `cfg_string` GUID，不保存新敏感配置。
- 不改变音轨 URI 或输入处理器。
- 新状态格式有版本号；未来升级不兼容时自动完整核对。
- macOS/Linux 源码和构建不变。

## 9. Rollback and Operational Notes

- 回滚点 1：`SubsonicClientWin` 请求快照和批量端点。
- 回滚点 2：独立 `LibraryImportState` 文件格式和原子提交。
- 回滚点 3：`BrowserWindow` 自动模式状态机和新按钮。
- 回滚点 4：递归 worker pool。
- 旧的 `dist/zh-CN-win-x64-bulk-import/` 产物保持只读；新产物写入独立目录。
- 状态文件属于可重建缓存；回滚 DLL 后它不会被旧版本读取，也不影响原有配置。

## 10. Deterministic Test Seams

- 将页序列校验、锚点匹配、差集和状态 codec 保持为不依赖 foobar UI 的纯逻辑，提供 Windows 测试程序。
- fake page source 覆盖：稳定多页、空尾页、旧专辑新 ID、预期单项重叠、非预期跨页重复、边界错位、scan token 改变和一次重试。
- 临时目录故障注入覆盖：截断、超限、重复 ID、错误 magic/版本、generation 冲突、正式替换失败和 RAII 清理。
- fake append receipt 覆盖：状态提交失败后补偿删除，以及补偿范围只包含本次追加。
- 并发测试覆盖：standalone/embedded 对同身份只有一个 lease；不同身份互不阻塞。

## 11. Trade-offs

- 空查询分页不能并发预取未知页数，但每 500 首只需一次请求，已消除现有数量级更高的艺术家/专辑往返。
- 精确 ID 集合占用 profile 磁盘和运行内存，但提供无假阳性的去重；不采用 Bloom filter，避免漏掉真正新增歌曲。
- 快速增量依赖 Navidrome 实现顺序；锚点、版本和完整核对是安全网，但不把实现特性误称为 OpenSubsonic 协议保证。
- scan token 与重叠分页会增加少量请求和一次边界重复传输，换取扫描期间集合变化的可检测性。
- 主线程补偿删除覆盖正常提交失败，但无法制造跨 foobar playlist 与文件系统的真正崩溃原子事务。
