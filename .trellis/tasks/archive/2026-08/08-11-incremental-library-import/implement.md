# 全库并发与增量导入实施计划

## 1. 开发前基线

- [ ] 运行 `trellis-before-dev`，确认任务状态、工作树、适用 spec 和用户既有未跟踪文件。
- [ ] 记录当前 `BrowserWindow`、`SubsonicClientWin`、`SubsonicTypes`、`Localization.h` 和 vcxproj 基线。
- [ ] 记录上一版 bulk-import DLL/安装包散列，并创建新的隔离构建树和输出目录，不覆盖旧产物。

## 2. 扩展类型与请求快照

- [ ] 在共享类型中增加 `MusicFolder`、`ServerInfo`、`ScanStatus` 及歌曲诊断字段，保持非 Windows 平台可编译。
- [ ] 在 `SubsonicClientWin` 增加不可变请求上下文捕获和显式 context 重载。
- [ ] 让导入任务中的 URL、认证和请求头全部来自同一快照；现有浏览入口行为不变。
- [ ] 增加 `getServerInfo`、`getMusicFolders`、`getScanStatus` 和 `getSongsPage(offset, 500)`。
- [ ] 对空 ID、页内重复、API 不支持和解析失败返回可区分错误，不把协议不支持混同为临时网络错误。

## 3. 实现版本化导入状态

- [ ] 新增 `Windows/LibraryImportState.h/.cpp`，实现身份散列、profile 路径、版本化二进制读写和长度上限。
- [ ] 保存完整非敏感身份、服务器类型/版本、音乐库指纹、cursorCount、精确 ID 集合和最多 64 个尾部锚点。
- [ ] 实现文件 generation、operation 临时文件、flush/close 校验、CAS 后原子替换和失败清理。
- [ ] 用 move-only `PreparedStateFile` RAII 守卫临时文件，只有正式提交成功才 disarm。
- [ ] 将新源文件注册到 `Windows/foo_navidrome.vcxproj`，保持公共 `/utf-8` 设置。

## 4. 实现分页完整核对

- [ ] 增加从 offset 0 顺序读取 500 首页面的枚举器，后续页面重叠 1 首，直到短页/空页。
- [ ] 验证预期重叠 ID；页内重复、其他跨页重复、边界错位或无进展必须使本轮失败，不能静默去重。
- [ ] 分页前后比较 `getScanStatus`；扫描中、lastScan 变化或边界漂移时丢弃并从头重试一次。
- [ ] 构建当前完整 ID 集合、尾部锚点和播放所需节点。
- [ ] 有旧状态时只生成差集候选；无状态时生成全库候选。
- [ ] 在后台准备新状态临时文件，完成载荷回主线程后一次追加并提交状态。

## 5. 实现 Navidrome 快速尾部增量

- [ ] 校验状态身份、服务器类型/版本、音乐库指纹和格式版本。
- [ ] 从 `cursorCount - 500` 附近开始，必要时按页向前回退寻找有序尾部锚点。
- [ ] 找到可信锚点后只收集其后的未知 ID，并继续到短页/空页。
- [ ] 更新 cursorCount、尾部锚点和精确 ID 集合；零新增也提交有效的新游标。
- [ ] 锚点缺失、顺序变化或分页异常时自动切换完整核对，不静默接受可疑结果。
- [ ] 快速路径复用同一重叠分页和 scan-token 校验，变化时丢弃并重试。

## 6. 实现兼容递归并发回退

- [ ] 保留现有递归行为作为空查询不支持时的兼容路径，但用请求快照重新获取艺术家根。
- [ ] 让 `collectSongsDeep` 显式接收请求 context，禁止回退路径读取运行中变化的 cfg 或复用旧 UI 根。
- [ ] 使用固定 4 worker、原子根索引、worker 局部结果和受锁合并。
- [ ] 保留逐根进度、失败统计和取消检查；不捕获裸 `BrowserWindow*`。
- [ ] 回退结果继续做精确 ID 差集和两阶段提交。

## 7. 更新 BrowserWindow 与本地化

- [ ] 将“添加全部”接入自动模式状态机，保持单项添加路径不变。
- [ ] 增加“完整核对”按钮、命令处理、忙碌保护和五按钮安全布局。
- [ ] 增加进程级 identity-scoped import lease；standalone 与 embedded 对同账户互斥，lease 随任务/载荷 RAII 释放。
- [ ] 扩展进度/完成载荷，区分普通队列、快速增量、完整核对和兼容回退，并持有状态守卫与 lease。
- [ ] 增量零候选时不调用 `enqueueNodes`，但提交有效状态并显示已是最新。
- [ ] 让播放列表追加返回 `PlaylistAppendReceipt`；正式状态提交失败/generation 冲突时，用 `playlist_remove_items(bit_array_range)` 同步回滚并验证数量。
- [ ] 在 `Localization.h` 集中新增全部按钮、进度、回退、完成和错误文本。

## 8. 静态与一致性验证

- [ ] FFF 可用时使用 FFF 搜索；若仍报 transport error，记录并回退 `git grep`。
- [ ] 严格 UTF-8 解码所有修改的 C++/头文件。
- [ ] 解析 vcxproj XML，确认 `/utf-8`、新 `.cpp/.h` 注册和既有平台配置。
- [ ] 运行 `git diff --check`。
- [ ] 审查后台闭包不捕获裸 `this`，所有完成消息检查 alive + operation id。
- [ ] 审查状态文件不包含 password、salt、token 或请求头值。
- [ ] 审查所有失败、消息丢弃、窗口销毁和取消分支都由 RAII 清理临时文件/lease 且不提交正式状态。
- [ ] 审查 scan token、预期重叠和跨页重复都以失败/重试处理，不存在“去重后继续提交”的分支。

建议命令：

```powershell
git diff --check
git grep -n -E "完整核对|检查新增|音乐库已是最新|状态提交" -- Windows
```

## 9. 确定性测试与故障注入

- [ ] 增加 Windows 测试程序，直接测试纯分页/锚点/差集逻辑和状态 codec。
- [ ] fake pages 覆盖稳定多页、零新增、旧专辑新 ID、短页、边界错位、跨页重复、scan token 改变和重试上限。
- [ ] 状态测试覆盖错误 magic/版本、截断、长度超限、重复 ID、散列身份不匹配、generation 冲突和临时文件 RAII 清理。
- [ ] fake playlist receipt 覆盖正式提交失败后的补偿删除及范围验证。
- [ ] 并发测试覆盖同身份 lease 互斥和不同身份独立运行。
- [ ] 使用 MSVC 构建并运行测试程序，要求 0 failed。

## 10. Windows x64 构建与打包

- [ ] 复制本任务产品文件到新的隔离构建树，复用已验证的 SDK/ATL/WTL sibling layout。
- [ ] 使用 Visual Studio Build Tools v145 运行 `Release|x64` `/t:Rebuild`。
- [ ] 验证 MSBuild 0 错误、DLL PE machine=x64、DLL 包含新增中文标记和状态模块符号。
- [ ] 在 `dist/zh-CN-win-x64-incremental-import/` 生成 DLL 与 `.fb2k-component`。
- [ ] 验证包仅含 `x64/foo_navidrome.dll`，内部 DLL 与外部 DLL SHA-256 一致。

构建命令基线：

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' `
  '<new-build-tree>\workspace\foo_navidrome\Windows\foo_navidrome.vcxproj' `
  /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 `
  /p:SolutionDir='<new-build-tree>\workspace\foo_navidrome\' `
  /p:OutDir='<new-build-tree>\out\x64\' /m /nologo /v:minimal
```

## 11. 行为验证

- [ ] 首次完整导入：全部可访问歌曲只追加一次并生成状态。
- [ ] 无变化二次运行：不追加，快速结束并显示已是最新。
- [ ] 新专辑新增歌曲：只追加新 ID。
- [ ] 旧专辑新增歌曲：只追加新 ID，不依赖 `newest` 专辑列表。
- [ ] 删除歌曲：不删除播放列表项目，后续游标仍可验证或自动完整核对。
- [ ] 强制完整核对：只追加未知 ID并替换当前完整基线。
- [ ] 修改服务器版本/用户/音乐库范围、破坏状态或锚点：自动完整核对。
- [ ] 分页期间触发 Navidrome 扫描或注入边界漂移：本轮丢弃并重试，不提交不完整基线。
- [ ] standalone 与 embedded 同时点击同一账户导入：一个运行、一个显示忙碌。
- [ ] 注入正式状态替换失败：本次播放列表追加被回滚，第二次不会出现重复。
- [ ] 关闭窗口/偏好页：不追加、不提交、不访问失效窗口。
- [ ] 单项添加、立即播放、搜索、刷新、右键、Enter 和流播放不回归。
- [ ] 若当前环境没有可控 Navidrome 测试库，明确把真实服务器的首次/二次/旧专辑新增三项烟测列为交付后的人工验证。

## 12. 交付与回滚

- [ ] 运行 Trellis check，按 AC1-AC18 逐项核对。
- [ ] 最终 diff 只包含本任务产品源码、任务文档和必要验证记录，不触碰无关未跟踪文件。
- [ ] 保留旧 bulk-import 产物并报告新 DLL/安装包绝对路径、大小、SHA-256 和未自动验证边界。
- [ ] 若快速顺序假设在用户服务器上不成立，可回滚自动模式到完整分页核对，状态文件仍可作为精确去重集合继续使用。
