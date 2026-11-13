# 原生兼容回归测试

需要 Windows、Visual Studio 2022 C++ 工具和 Windows SDK。测试程序不显示窗口，输出
UTF-8 JSON；配置文件由脚本写成 UTF-16 LE。输出目录使用独立测试目录，不要指向游戏。

## 构建

在仓库根目录执行：

```powershell
.\tests\build-probes.ps1 -Architecture x86 -OutputDirectory E:\NepgearTests\probes\x86
.\tests\build-probes.ps1 -Architecture x64 -OutputDirectory E:\NepgearTests\probes\x64
```

`-VisualStudioDirectory` 可指定 VS 安装目录。DLL 可用 `build.bat` 构建；未压缩的调试
验证可直接调用 MSBuild 并传 `/p:NepgearSkipPack=1`。测试不需要修改游戏或安装字体。

## 区域模拟

先从官方发布包取得同版本的四个 LEP DLL。`RuntimeDirectory` 指向直接包含这些 DLL 的
目录，而不是 ZIP 或上一级目录。

```powershell
.\tests\run-locale-integration.ps1 `
  -Dll .\Release\winmm.dll `
  -Probe E:\NepgearTests\probes\x86\native_hook_probe.exe `
  -RuntimeDirectory E:\LEP206\LocaleEmulatorPlus `
  -OutputDirectory E:\NepgearTests\runs\lep206-x86 `
  -CodePage 932 `
  -ChildProbe E:\NepgearTests\probes\x64\native_hook_probe.exe
```

反向跨位数测试使用 x64 DLL/父探针和 x86 `ChildProbe`。`-CodePage 936` 同时检查中文
LCID/charset/时区。移除 `ChildProbe` 可验证普通转区。更换 `RuntimeDirectory` 为官方
v2.0.0.0 目录可检查早期 2.x 回退，日志应出现 `retrying LEPB v2/204`。

脚本断言 ACP/OEMCP、用户/系统 LCID、bias、bootstrap、cwd 和原 Unicode 参数。
跨位数测试进一步检查异位数子进程中的继承 Core。`-ExpectFailure` 用于旧 DLL 的
复现对照。详细输出为 `locale-result.json`、`child-locale-result.json` 和 `Nepgear.log`。

## 字体、名称与代理导出

```powershell
.\tests\run-font-integration.ps1 `
  -Dll .\Release\winmm.dll `
  -Probe E:\NepgearTests\probes\x86\font_bgi.exe `
  -OutputDirectory E:\NepgearTests\runs\font-bgi-x86 `
  -Engine Bgi
```

| 探针 | Engine | 主要断言 |
| --- | --- | --- |
| `font_generic.exe` | Generic | 通用替换、非目标回退、raw winmm EAT 代码入口 |
| `font_bgi.exe` | Bgi | OTM A/W 原请求名、一次重建、缓冲区边界与真实 full-name |
| `font_topcat.exe` | TopCat | x86 度量上限 560 |
| `font_krkr.exe` | Krkr | 原脚本成员名与 TFT 可见性 |

`-FontDisabled` 和 `-EngineDisabled` 检查开关透传。x64 使用对应架构的 DLL/探针并加
`-X64`；TopCat 应回退原度量。`-ExpectFailure` 可复现旧 BGI/KRKR DLL 的问题。结果
写入 `font-result.json` 与 `Nepgear.log`，包含 raw winmm 调用与 x86 栈平衡结果。

## KRKR ABI

```powershell
.\tests\run-krkr-abi.ps1 `
  -Dll .\Release\winmm.dll `
  -Probe E:\NepgearTests\probes\x86\krkr_cdecl.exe `
  -Plugin E:\NepgearTests\probes\x86\krkr_plugin_fixture.dll `
  -OutputDirectory E:\NepgearTests\runs\krkr-cdecl-x86 `
  -Mode 0
```

`krkr_stdcall.exe` 验证 x86 stdcall 或 x64 平台约定。探针提供合成 TVP exporter、原生
map/unmap 对象和插件 `V2Link`，检查 result、参数、同实例 unmap、栈平衡及无关调用。

| 探针/模式 | 额外参数 | 预期 |
| --- | --- | --- |
| stdcall/cdecl，0 | 无 | map 转原生 unmap |
| stdcall/cdecl，2 | `-ExpectNative` | 原生 map |
| `krkr_old.exe`，0 | `-ExpectNative` | TVP 1.3 自动回退 |
| `krkr_unknown.exe`，0 | `-ExpectNative` | 未知版本自动回退 |
| `krkr_unknown.exe`，1 | 无 | 显式跳过版本门控，按接口能力绑定 |

结果写入 `krkr-result.json` 和 `Nepgear.log`。不同架构或模式使用不同输出目录，避免
旧 JSON 与日志影响观察。

## 验证结论的边界

这些测试使用真实 Nepgear DLL、官方 LEP 发布 DLL、Windows GDI 和合成引擎夹具。
它们验证 ABI、启动参数、名称与开关行为，不代表每款游戏的字体画面或资源格式实测。
发布包还应对最终 UPX DLL 运行上述 smoke 测试，确认压缩后入口与转区仍正常。
