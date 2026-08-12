# foo_navidrome Windows 中文增强版

**简体中文** | [English](README_EN.md)

这是 [santiagorod92/foo_navidrome](https://github.com/santiagorod92/foo_navidrome) 的 Windows 中文增强版。它是一个 [foobar2000](https://www.foobar2000.org/) 组件，可在 foobar2000 中浏览并播放 [Navidrome](https://www.navidrome.org/) 或其他兼容 [Subsonic](http://www.subsonic.org/) API 的音乐服务器。

本 Fork 主要面向 **foobar2000 v2 for Windows**，保留上游提交历史，并在上游 Windows 版本基础上增加中文界面、整库导入、增量入库、封面显示和 ESLyric 歌词支持。

## 主要功能

- **简体中文界面**：Navidrome 参数页、媒体库浏览器、状态提示和错误信息均已汉化。
- **浏览音乐库**：按艺术家 -> 专辑 -> 歌曲逐级展开，也可直接搜索艺术家、专辑或歌曲。
- **添加全部**：一键把服务器中的所有文件夹或全部歌曲导入 foobar2000 播放列表。
- **增量导入**：完成一次整库入库后，再次导入时只处理 Navidrome 中新增的歌曲，减少重复扫描和等待时间。
- **播放列表操作**：支持添加到播放列表、立即播放和双击歌曲播放。
- **封面显示**：从 Navidrome 获取封面，可在 foobar2000 的正在播放界面和播放列表中显示。
- **ESLyric 歌词**：自动安装兼容 [ESLyric](https://github.com/ESLyric/release) 的 Navidrome 歌词脚本，支持同步歌词和普通歌词。
- **稳定播放地址**：播放列表保存 `navidrome://track/<id>?...` 地址，修改服务器密码或地址后无需重新建立播放列表。
- **连接测试**：可在参数页直接检查服务器地址和账号是否可用。

## 系统要求

- Windows 10/11 x64
- [foobar2000 v2 for Windows](https://www.foobar2000.org/)
- Navidrome 或其他兼容 Subsonic API 的服务器
- 可选：[ESLyric](https://github.com/ESLyric/release)，用于显示歌词

> 本 Fork 目前只维护和验证 Windows x64 版本。上游项目还支持 macOS、Windows x86、Windows ARM64EC 和 Wine，相关说明请查看[英文文档](README_EN.md)或[上游仓库](https://github.com/santiagorod92/foo_navidrome)。

## 安装

### 安装组件包

1. 从本仓库的 [Releases](https://github.com/Elax-391/foo_navidrome/releases) 页面下载 Windows x64 的 `.fb2k-component` 文件。
2. 双击安装包，或将它拖入 foobar2000。
3. 按 foobar2000 提示确认安装并重启。

### 手动安装 DLL

也可以把 `foo_navidrome.dll` 复制到：

```text
%APPDATA%\foobar2000-v2\user-components-x64\foo_navidrome\
```

完整路径通常为：

```text
C:\Users\<用户名>\AppData\Roaming\foobar2000-v2\user-components-x64\foo_navidrome\foo_navidrome.dll
```

复制后重新启动 foobar2000。

## 配置 Navidrome

1. 打开 **文件 -> 参数选项**。
2. 进入 **工具 -> Navidrome**。
3. 填写服务器地址，例如 `http://192.168.1.10:4533/`。
4. 填写 Navidrome 用户名和密码。
5. 如服务器需要额外 HTTP 请求头，在对应配置项中填写。
6. 点击 **测试连接**；连接成功后点击 **应用** 或 **确定**。

## 浏览和导入音乐

组件会出现在：

```text
参数选项 -> 媒体库 -> Navidrome
```

常用操作：

- 展开艺术家查看专辑，再展开专辑查看歌曲。
- 在顶部搜索框中搜索艺术家、专辑或歌曲。
- 选中条目后点击 **添加到播放列表** 或 **立即播放**。
- 点击 **添加全部**，把完整音乐库一次性加入播放列表。
- 双击歌曲可立即播放。

### 增量导入规则

首次点击 **添加全部** 时，组件会扫描完整音乐库并记录已成功导入的歌曲。后续再次执行时：

- 只添加服务器中新出现的歌曲；
- 已入库歌曲不会重复添加；
- 如果服务器身份或账号发生变化，会使用对应的新导入状态；
- 导入失败或被取消时，不会把未完成的结果标记为成功。

如果确实需要重新执行完整导入，可在 Navidrome 参数页重置导入状态后再次运行。

## 封面

播放 `navidrome://` 歌曲时，组件会根据歌曲或专辑的 `coverArt` 标识从 Navidrome 获取封面。只要当前 foobar2000 布局中包含封面显示组件，就能在正在播放界面显示封面；播放列表是否显示封面取决于所用播放列表组件和列配置。

如果封面没有显示，请检查：

1. Navidrome 中对应专辑是否已有封面；
2. foobar2000 是否已启用封面查看面板；
3. 服务器地址和认证信息是否仍然有效；
4. 歌曲是否由本组件以 `navidrome://` 地址加入播放列表。

## ESLyric 歌词

歌词功能通过 [ESLyric](https://github.com/ESLyric/release) 组件显示。

1. 安装适用于当前 foobar2000 架构的 ESLyric。
2. 打开一次 **工具 -> Navidrome** 参数页并保存配置。
3. 本组件会在 foobar2000 配置目录下生成 Navidrome 歌词源脚本和不包含明文密码的配置文件。
4. 在 foobar2000 布局中加入 ESLyric 面板。
5. 播放一首由本组件添加的 Navidrome 歌曲，ESLyric 会使用歌曲 ID 获取歌词。

脚本目录通常位于：

```text
%APPDATA%\foobar2000-v2\eslyric-data\scripts\
```

若服务器支持结构化歌词，时间轴会转换为 LRC；不支持时会自动回退到传统歌词接口。

## Windows 构建

### 依赖

- Visual Studio 2022
- **使用 C++ 的桌面开发**工作负载
- MSVC v143 工具集
- Windows SDK
- [reupen/foobar2000-sdk-unmodified](https://github.com/reupen/foobar2000-sdk-unmodified)

将 SDK 和本仓库放置为以下结构：

```text
工作目录/
├── pfc/
└── foobar2000/
    ├── SDK/
    ├── helpers/
    ├── shared/
    ├── foobar2000_component_client/
    └── foo_navidrome/              <- 本仓库
```

从 `foo_navidrome` 目录看，工程依赖以下相对路径：

```text
../SDK/
../helpers/
../shared/
../foobar2000_component_client/
../../pfc/
```

### 编译步骤

1. 用 Visual Studio 2022 打开 `Windows/foo_navidrome.vcxproj`。
2. 如本地 SDK 工程 GUID 与项目文件不一致，更新 `.vcxproj` 中的 `<ProjectReference>` GUID。
3. 选择 **Release | x64**。
4. 生成项目，得到 `foo_navidrome.dll`。
5. 将 DLL 安装到 foobar2000 的 `user-components-x64\foo_navidrome` 目录并重启 foobar2000。

项目也定义了 Win32 和 ARM64EC 配置，但本 Fork 当前只承诺 Windows x64 的维护与验证。

## 仓库关系

- `origin`：[Elax-391/foo_navidrome](https://github.com/Elax-391/foo_navidrome)
- `upstream`：[santiagorod92/foo_navidrome](https://github.com/santiagorod92/foo_navidrome)

同步上游时，建议先拉取 `upstream/main`，检查 Windows 相关差异和冲突，再合并到本 Fork。Windows 中文增强功能主要位于 `Windows/` 目录。

## 项目结构

```text
foo_navidrome/
├── Windows/
│   ├── BrowserWindow.h/.cpp          # Windows 音乐库浏览界面
│   ├── NavidromePluginWin.cpp        # 参数页、服务注册、播放与封面获取
│   ├── SubsonicClientWin.h/.cpp      # WinHTTP Subsonic 客户端
│   ├── LibraryImporter.h/.cpp        # 整库与增量导入
│   ├── LibraryImportState.h/.cpp     # 增量导入状态
│   ├── MediaEnrichmentLogic.h/.cpp   # 封面与歌词 URI 解析逻辑
│   ├── EsLyricBridge.h/.cpp          # ESLyric 脚本与配置桥接
│   ├── eslyric_scripts/              # ESLyric Navidrome 歌词源
│   └── foo_navidrome.vcxproj         # Visual Studio 工程
├── dist/                             # 已生成的 Windows 构建产物
├── scripts/                          # 构建、安装和 CI 辅助脚本
├── README.md                         # 默认中文文档
└── README_EN.md                      # 英文文档
```

## 已知说明

- “添加全部”的首次完整扫描速度取决于音乐库规模、服务器性能和网络延迟；之后的增量导入会明显减少重复工作。
- 封面和歌词依赖服务器中已有的媒体元数据；服务器没有封面或歌词时，组件无法凭空生成。
- ESLyric 是独立第三方组件，不包含在本仓库中，需要单独安装。
- 本 Fork 的 Windows 功能与上游后续改动可能产生差异，合并上游更新前应先完成构建和运行测试。

## 参与开发

欢迎提交 Issue 和 Pull Request。提交前请：

1. 保持改动聚焦于一个问题；
2. 使用 [Conventional Commits](https://www.conventionalcommits.org/) 格式编写提交信息；
3. 至少完成一次 **Release | x64** 构建；
4. 对导入、封面或歌词改动运行对应测试；
5. 不要在仓库中提交 Navidrome 密码、令牌、私有服务器地址或用户配置。

## 许可证

本组件自身源代码采用 [MIT License](LICENSE)。原作者版权和许可证声明均予以保留。

项目中附带的 WTL 使用 Microsoft Public License（MS-PL），详见 `Windows/third_party/wtl/` 中的声明。foobar2000 SDK 与 PFC 不属于本仓库 MIT 许可证覆盖范围，它们由各自作者按独立条款提供，且不会随本仓库源码一同分发。
