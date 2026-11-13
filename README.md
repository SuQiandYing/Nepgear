# Nepgear

面向日文 Galgame 的 DLL 劫持与 Hook 框架。伪装成 `winmm.dll` 随游戏启动，提供区域模拟、字体替换、文本转换、文件重定向和引擎专用适配。

支持 x86 / x64。多数 Galgame 为 32 位，通常用 x86 版本。

## 功能概览

| 模块 | 说明 |
| --- | --- |
| DLL 代理 | 伪装 `winmm.dll`，导出转发给系统真实 DLL，不改动游戏文件 |
| 区域模拟 | 内置 Locale Emulator Plus，以日文 locale 重启进程 |
| 字体 Hook | 替换字体、缩放、字重、字距、垂直度量、字符集伪装 |
| 文本替换 | 日文旧字体↔繁体、繁简互转，共三张映射表 |
| 代码页重定向 | 在 `MultiByteToWideChar` / `WideCharToMultiByte` 边界改码页 |
| 引擎适配 | 16 个引擎的专用字体处理（见下表） |
| 窗口标题 | 三种模式改写标题，支持 Unicode |
| VFS 与重定向 | 优先读外部文件夹或 `.chs` 封包 |

### 已适配引擎

BGI / Buriko、Artemis（含 Legacy）、KiriKiri / TVP、EntisGLS、Softpal、Escu:de、Mirai、Majiro、DxLib、TinkerBell、TopCat（x86）、CatSystem2、Unity（Mono / IL2CPP / TextMeshPro）、TyranoScript 5 / Electron、Ren'Py、YU-RIS。

引擎开关默认全关，需要哪个开哪个，且受 `[Font] Enable` 总开关约束。各引擎实现细节见 `Nepgear/hooks/internal/font/engines/*/README.md`。

## 构建

需要 Visual Studio 2022（v143 工具集）和 Windows SDK。

```bat
build.bat              两个架构 Release
build.bat x86          仅 32 位
build.bat x64          仅 64 位
build.bat all rebuild  全量重建
build.bat x86 debug    Debug 配置
```

`build_x86.bat` / `build_x64.bat` 是单架构的简写入口。也可以直接用 Visual Studio 打开 `Nepgear.sln`，三个工程（Nepgear、Packer、Unpacker）都在里面。

产物：

| 架构 | 输出目录 |
| --- | --- |
| x86 | `Release\winmm.dll`、`Packer.exe`、`Unpacker.exe` |
| x64 | `x64\Release\winmm.dll`、`Packer.exe`、`Unpacker.exe` |

Release 构建有两点和常规工程不同。一是 `winmm.dll` 出来就是 UPX 压缩版（约 39%），压缩内置在 MSBuild 流程里，先压临时副本、`upx -t` 校验通过才覆盖输出，中断不会留下损坏文件。二是不生成 `.pdb` / `.lib` / `.exp`，输出目录只有可执行文件。

需要 PDB 调试时设 `NEPGEAR_SKIP_PACK=1` 再用 `build.bat` 构建，或编 Debug 配置。直接调用 MSBuild 时使用 `/p:NepgearSkipPack=1`。

UPX 5.1.1 随仓库放在 `tools/upx/`。

## 部署

复制到游戏主 EXE 所在目录：

1. `winmm.dll` — 架构必须与游戏进程一致
2. `Nepgear.ini` — 配置文件，**必须存为 UTF-16 LE**，缺失会直接弹窗退出
3. 字体文件 — 文件名与 ini 里的 `FileName` 一致

游戏启动时会加载同目录下的 `winmm.dll`，Nepgear 把原有函数调用转发给系统真正的 `winmm.dll`，所以不影响音频功能。

开启 `[LocaleEmulator] Enable=1` 时还需要把官方 LEP 2.x 的运行时 DLL 放进重定向文件夹（默认 `Nepgear/`）。同位数启动至少需要对应的 `LoaderDll` 与 `LocaleEmulatorPlus`；跨位数子进程需要同时提供同版本的 `LoaderDll_x86.dll`、`LocaleEmulatorPlus_x86.dll`、`LoaderDll_x64.dll`、`LocaleEmulatorPlus_x64.dll`。这些运行时不随仓库分发。

已验证官方 LEP **v2.0.6.0**，并兼容 v2.0.0.0 的早期 2.x 配置接口。接口说明和字体更新对照见 [兼容更新说明](docs/compatibility.md)。

## 配置

完整带注释的配置见仓库根目录的 `Nepgear.ini`，这里只列关键项。

### `[System]`

```ini
Enable=1              ; 总开关，0 = 全部功能停用
ShowStartupPopup=1    ; 启动提示弹窗
```

### `[Font]`

```ini
Enable=1
FileName=galgame.ttf  ; 放在游戏根目录或重定向文件夹，留空用系统字体
FaceName=galgame      ; CreateFont 的 FaceName，建议与字体内部名一致
Charset=128           ; 128=Shift-JIS  134=GB2312  1=Default

EnableFaceNameReplace=1
EnableCharsetReplace=0

EnableHeightScale=1
HeightScale=1.0       ; 支持小数，修正汉化后字体过大过小
EnableWidthScale=1
WidthScale=1.0
EnableWeight=1
Weight=0              ; 0=沿用程序请求  400=标准  700=粗体
```

字距、垂直度量、行距：`EnableCharSpacing` / `CharSpacing`、`EnableVerticalMetrics` / `AscentPermille` / `DescentPermille`、`EnableLineSpacing` / `LineSpacing`。垂直度量按 unitsPerEm 千分比修补 hhea 和 OS/2 表。

字符集伪装与文本替换：

```ini
EnableCodepageSpoof=0   ; 把游戏请求的 SpoofFromCharset 伪装成 SpoofToCharset
SpoofFromCharset=128
SpoofToCharset=1

EnableTextSubstitution=0
TextSubstitutionMode=0  ; 0=日文旧字体/异体字  1=繁转简  2=简转繁
TextSubstitutionCodepage=932
```

### `[FontCompatibility]`

个别引擎对某些 GDI Hook 敏感，这里逐项关闭：`SkipDrawTextA`、`SkipFontDataQueries`、`SelectObjectTrackedOnly`、`HookCreateFontW`、`HookCreateFontIndirectW`、`HookGetTextFace`。字体不生效或渲染异常时优先调这一节。

### `[FontEngines]`

每个引擎一个 `Enable*` 开关加若干细项。例如：

```ini
EnableBgiHook=0
BgiPatchGdiImports=1              ; Win32 专用，x64 无效
BgiClearGlyphCacheOnSwitch=1

EnableKrkr=0
KrkrDisablePrerenderedFonts=1
KrkrNativeDispatchMode=0          ; 0=自动 TVP 1.4.x  1=跳过版本门控  2=禁用

EnableTopCatHook=0                ; x86 专用，验证 GDI 导入和调用点后启用

EnableArtemis=0
ArtemisPatchTables=1              ; 改 list_windows*.tbl / IET 文本里的字号、字距、行距
ArtemisRedirectFontFiles=1
ArtemisFontSize=0                 ; 0 = 保留游戏原值
ArtemisRubySize=-1                ; -1 = 保留游戏原值
```

BGI 会在 `GetOutlineTextMetricsA/W` 中保留原请求字体名，避免替换字体触发引擎递归重建；系统 USER32 字体请求跳过 BGI 专用度量归一化。KRKR 保留原生 `mapPrerenderedFont` 成员名与 TFT 文件，在接口兼容时把字体映射请求转到同一字体实例的原生 unmap；旧版或未知版本在自动模式下保留原生路径。TopCat 的度量保护仅作用于确认过的 x86 调用点。

其余引擎（EntisGLS、Softpal、Escu:de、Mirai、Majiro、DxLib、TinkerBell、Unity、Tyrano、Ren'Py、YU-RIS）的开关命名同理，逐项注释见 `Nepgear.ini`。

CatSystem2 和 YU-RIS 没有独立总开关：前者在 `Enable=1` 且 `EnableFaceNameReplace=1` 时自动检测，后者随字体替换启用；YU-RIS 另有 `YurisAtlasCodepage` 控制图形集码页（0 = 按资源结构自动判断）。

### `[Window]`

```ini
Enable=1
TitleMode=2   ; 0=进程内同步  1=仅 WinEvent 监听  2=混合（推荐）
Title=自定义标题
```

### `[FileRedirect]` 与 `[FileHook]`

```ini
Enable=1
Folder=Nepgear            ; 优先从这里找文件
ArchiveFile=Nepgear.chs   ; 封包模式的资源包

VFSMode=0                 ; 0=物理读取  1=内存读取
```

### `[LocaleEmulator]`

```ini
Enable=0
CodePage=932              ; 932=Shift-JIS  936=GBK
LocaleID=1041             ; 1041=日本  2052=中国
Charset=128
RegistryRedirectionMode=2 ; 0=核心  1=基础  2=高级
HookUILanguageMode=1     ; 0=关闭  1=Win32 API  2=Native API + MUI
Timezone=Tokyo Standard Time
```

按当前进程位数加载匹配的 `LoaderDll` 与核心 DLL，并暂存已提供的另一位数组以支持子进程。优先传入 LEPB v4/416，早期 2.x Loader 拒绝该结构时回退到 v2/204。运行时暂存到系统临时目录。`Timezone` 使用 Windows 时区 ID；未知 ID 会报告配置错误。

### `[CodePage]`

```ini
Enable=0
FromCodePage=932
ToCodePage=65001
```

用于引擎硬编码 CP932 解码 UTF-8 脚本的场合。字符集伪装发生得太晚时（文本在 GDI 之前就已成乱码），改这里。

### `[GLOBAL]`

KiriKiri XP3 补丁与 RioShiina 专用：`EnableKrkrz` / `KrkrzPatchFile`、`EnableRioShiina` / `RioShiinaMode`（1 = 翻译重定向，2 = 解包）等。

### `[Debug]`

```ini
Enable=0                  ; 弹出 CMD 控制台显示 Hook 日志
LogToFile=0               ; 输出 Nepgear.log
EnableFontTrace=0         ; 字体 Hook 细粒度采样
FontSlowMs=50
FontTraceSampleLimit=0
```

字体不生效时先开 `Enable=1` 看日志，能看到实际拦到哪些 API、字体是否加载成功。

## 配套工具

`Packer.exe` 把翻译资源打成单个 `.chs`：保持原目录结构放好文件，把文件夹拖到 `Packer.exe` 上，同级生成同名 `.chs`，再在 ini 里配 `ArchiveFile`。

`Unpacker.exe` 反向解包，用于检查封包内容。

两者都是命令行工具，直接拖拽即可。

## 目录结构

```
Nepgear/            主 DLL 工程
  dllmain.cpp       入口与 Hook 安装顺序
  hooks/            各子系统聚合单元
    internal/       .cppinc 实现切片（不单独编译）
      font/engines/ 16 个引擎适配
      locale/       LEP 2.x 动态调用 ABI
  font/             字体表修补（name / cmap / OS/2 / hhea）
Common/             Packer 与 Unpacker 共用头文件
Packer/ Unpacker/   资源打包工具
tools/upx/          随仓库固定的 UPX 5.1.1
tests/              原生区域、字体和 KRKR 接口回归探针
```

代码组织约定见 `Nepgear/hooks/README.md`。`.cppinc` 是实现切片，与所属 `.cpp` 共享私有状态和原始 API 指针，不能单独编译。

兼容测试的构建与执行方法见 [tests/README.md](tests/README.md)。
