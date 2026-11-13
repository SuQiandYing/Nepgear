# LEP 2.x 与字体兼容更新

本次更新基于 Nepgear `54b1a9b`，字体实现对照 SimpleFontHook `9d4a592` 及其 2.1–2.9
发布说明。区域集成使用官方 LEP v2.0.6.0 发布 DLL 验证，并验证 v2.0.0.0 的早期 2.x 回退。

## LocaleEmulatorPlus

### 问题与修复

原 Nepgear 传入没有 Size/Version 的 264 字节旧 LEPB，并使用额外包含
`FirstCallLdrLoadDll` 的进程信息结构。LEP 2.x 改为固定、带版本的配置块和 Windows
标准 `PROCESS_INFORMATION`。官方 v2.0.6.0 Loader 收到旧配置时返回
`STATUS_INVALID_PARAMETER (0xC000000D)`，进程保持宿主区域设置。

`hooks/internal/locale/lep_api.h` 现在声明以下动态调用接口，并在两个架构作编译期检查：

| 配置接口 | 字节数 | 时区表示 |
| --- | --- | --- |
| LEPB v4 | 416 | Windows 时区 ID 和可选名称覆盖 |
| LEPB v2 | 204 | 固定时区规则与名称 |
| PROCESS_INFORMATION | x86 16 / x64 24 | Windows 标准进程和线程句柄、ID |

优先调用 `LepCreateProcess2`，缺少该导出时使用 `LepCreateProcess`。先传 v4；只有返回
`STATUS_INVALID_PARAMETER` 时才按 v2 重试一次，不再使用旧版 264 字节接口。

配置检查覆盖有效代码页、LCID、字体 charset 和 Windows 时区 ID。未知时区会报告错误，
不会默默采用零偏移。v2 保留标准与夏令时 bias；东京名称与 v4 Loader 一致。

是否重启的判断同时比较 ACP/OEMCP、用户/系统 LCID、开启模拟后的 UI 语言和时区，
避免 ACP 恰好一致时漏掉其他区域配置。原 Unicode 参数、工作目录和 Nepgear bootstrap
标记继续传递；Soft-Denchi 的延迟 helper 路径使用同一套新接口。

### 配置与部署

```ini
[LocaleEmulator]
Enable=0
CodePage=932
LocaleID=1041
Charset=128
RegistryRedirectionMode=2
HookUILanguageMode=1
Timezone=Tokyo Standard Time
```

| 配置键 | 值与默认 |
| --- | --- |
| `RegistryRedirectionMode` | 0 核心（3 项），1 基础（4 项），2 高级（8 项，默认） |
| `HookUILanguageMode` | 0 关闭，1 Win32 API（默认），2 Native API + MUI |

模式范围为 0–2；非法值分别恢复为 2 和 1。模式具体钩子由对应版本的 LEP Core 实现。

把官方同版本的四个 DLL 放进 `[FileRedirect] Folder` 指定的资源目录（默认 `Nepgear`）：

```text
LoaderDll_x86.dll
LocaleEmulatorPlus_x86.dll
LoaderDll_x64.dll
LocaleEmulatorPlus_x64.dll
```

同位数启动只要求对应的一对 DLL。提供另一对时也会暂存到同一临时目录，供 LEP 的跨
位数子进程 broker 查找。只暂存本位数 DLL 会造成异位数子进程恢复宿主区域；本次已用
x86→x64、x64→x86 的真实 CreateProcess 调用复现并修复。

## SimpleFontHook 发布对照

| 上游发布 | 更新 | Nepgear 处理 |
| --- | --- | --- |
| 2.1–2.2 | 引擎资源适配、YU-RIS 位图与 CatSystem2 字体路径 | 已有对应实现，保留 Nepgear 的文件/VFS 桥接 |
| 2.3 | BGI 解码后 LINE 字体记录适配 | 已有 x86/x64 语义布局处理，继续保留 |
| 2.4 | TyranoScript 适配、UPX | 已有运行时适配和工程压缩流程 |
| 2.5 | 繁简映射后回想文本断行 | 已有对应文本边界处理，避免重复覆盖 |
| 2.6 | KRKR 原生成员保留、cdecl/stdcall 兼容 | 同步原生 map/unmap 桥接，停止改名和隐藏 TFT |
| 2.7 | BGI OTM 名称与 USER32 探测、KRKR 版本门控 | 同步；改进 A/W 变长名称缓冲区，自动模式限 TVP 1.4.x |
| 2.8 | TopCat 字体适配 | 新增 x86 IAT/调用点适配和 `EnableTopCatHook` |
| 2.9 | x86 winmm 导出转发兼容 | Nepgear 已使用代码入口；验证 raw EAT 调用与 x86 栈平衡 |

对照范围为字体和运行时兼容代码。SimpleFontHook 独立 GUI 的动画、界面与品牌逻辑不
属于 Nepgear 的 DLL 字体模块。

### BGI

保留 `GetOutlineTextMetricsA/W` 返回的原请求 face name，避免引擎不断重建替换字体。
原名称追加到缓冲区尾部，同步 size query 和 `otmSize`，保持真实 family/full/style 名称
与偏移，ANSI 路径支持多字节名称。USER32 请求跳过 Win32 BGI 专用度量归一化。

继续使用现有 `[FontEngines] EnableBgiHook`、`BgiPatchGdiImports` 和
`BgiClearGlyphCacheOnSwitch`，不增加重复开关。总开关默认关闭。

### KiriKiri / TVP

```ini
[FontEngines]
EnableKrkr=0
KrkrDisablePrerenderedFonts=1
KrkrNativeDispatchMode=0
```

模式 0 自动验证 TVP 1.4.x；1 跳过版本门控并尝试接口绑定；2 禁用原生分派桥接。模式 1
仍需通过身份、exporter 和方法验证。未知、旧版或能力缺失时自动保留原生 map/unmap 与
TFT 文件。字体总开关、名称替换、非空字体名和引擎开关共同约束桥接。

插件 V2Link 包装保持各插件原入口独立；x86 适配 stdcall/cdecl，x64 使用平台约定。
只有确认的 native map 调用转到同实例 native unmap，其他对象和成员透传。

### TopCat

```ini
[FontEngines]
EnableTopCatHook=0
```

默认关闭，仅支持已验证的 x86 PE/IAT 调用约定。身份要求 TOPCAT + SET_FONT 或
TCD3 归档头 + SET_FONT；两个 GDI 导入和直接调用点均通过验证才安装。匹配调用点的
字形宽度和高度受 560 像素上限保护。关闭字体/名称替换/引擎开关时保护透传，x64 使用
通用字体路径。

## Nepgear 集成边界

- 文件相关适配继续通过 FileEngineBridge 和单一 `file_hook.cpp` Detours 所有者进入，
  保留 A/W 路径、资源包与 VFS 行为。
- 保留内部字体探测保护，避免测量源字体时再次触发替换。
- `GetProcAddress` 依次执行 KRKR 与 YU-RIS 包装，两个运行时路径同时保留。
- TopCat 的 IAT 修改安排在通用字体 Detours 事务提交成功后。
- 引擎细分选项仍受字体总开关约束，原有引擎默认开关不变。

## 验证范围

测试构建与命令见 [tests/README.md](../tests/README.md)。原生进程与真实 DLL 验证包括：

- 官方 LEP v2.0.6.0：x86/x64 日文和中文 ACP/OEMCP、LCID、时区、Unicode 参数、工作目录。
- 官方 v2.0.0.0：两个架构的 LEPB v2 回退。
- v2.0.6.0 跨位数子进程：x86→x64 与 x64→x86 的区域继承。
- BGI GDI 夹具：A/W 逻辑名称、一次重建收敛、size/边界、小缓冲区与真实 full-name 保留。
- KRKR 接口夹具：两种 x86 约定、x64、原生 map/unmap 分派、栈、参数、结果及模式回退。
- TopCat 夹具：x86 正向、非目标、字体关闭、引擎关闭、x64 回退。
- raw winmm EAT：`timeGetTime`、`timeGetDevCaps`、`mciGetDeviceIDW` 及 x86 栈平衡。

测试使用原生 GDI、合成引擎 exporter/标记和官方 LEP 发布 DLL，没有实际游戏样本。
结论覆盖上述接口和启动行为，不等于所有游戏画面、Soft-Denchi 包装器或每个引擎资源
格式的完整实测。

上游来源：[SimpleFontHook releases](https://github.com/SuQiandYing/SimpleFontHook/releases)、
[LEP v2.0.6.0](https://github.com/julixian/LocaleEmulatorPlus/releases/tag/v2.0.6.0)、
[LEP v2.0.0.0](https://github.com/julixian/LocaleEmulatorPlus/releases/tag/v2.0.0.0)。
