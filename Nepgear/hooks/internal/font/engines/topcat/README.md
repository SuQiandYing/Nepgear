# TopCat 字体兼容适配器

## 职责

`topcat_font.cppinc` 适配 TopCat 的 x86 GDI 字体创建和固定字形缓冲区。字体创建进入
通用 `CreateFontIndirectA` 替换路径；只在已确认的引擎度量调用点限制异常宽度和高度，
避免替换字体超出 560 像素的缓冲区上限。

## 配置

```ini
[FontEngines]
EnableTopCatHook=0
```

默认关闭。安装要求字体总开关、名称替换和 TopCat 开关开启，且替换字体名非空。
字体或引擎开关关闭时度量包装透传原数据。x64 不安装此专用适配，继续使用通用字体钩子。

## 身份与能力

- 身份要求 TOPCAT + SET_FONT 模块标记，或真实 TCD3 归档头 + SET_FONT。
- 能力要求有效的 x86 PE、`gdi32.dll` 中 `CreateFontIndirectA` 和 `GetTextMetricsA` 的 IAT，
  以及两个入口各自 1–16 个经过验证的直接调用点。
- 普通 TCD 后缀、单个 TOPCAT 字符串和通用 GDI 导入不会单独激活适配器。
- 扫描限定于映像和可执行节；所有 RVA、名称、IAT 槽和调用点均作边界检查。

## 安装与运行

通用字体 Detours 事务提交成功后才安装 TopCat 的两个主模块 IAT 包装。若第二个 IAT
写入失败，恢复第一个槽。字体名复制保证 `LOGFONTA::lfFaceName` 终止。

度量包装先调用原始 `GetTextMetricsA`。只有匹配已保存返回地址时才归一化异常
`tmMaxCharWidth`，并把宽度和高度限制在 1–560；其他调用点保留原数据。身份、导入或
调用点能力缺失时返回原路径。日志有采样上限，高频度量路径不扫描文件。

## 验证

原生夹具验证正向 x86 字体替换与度量上限、非目标程序、字体总开关关闭、TopCat 开关
关闭，以及 x64 回退。执行方法见仓库根目录 `tests/README.md`。

本适配器同步自 SimpleFontHook 2.8。发布说明中的游戏样本由上游报告，本次使用接口
夹具验证，没有对应游戏样本的画面实测。
