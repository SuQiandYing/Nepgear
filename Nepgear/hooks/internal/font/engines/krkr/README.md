# KiriKiri / TVP 字体兼容适配器

## 职责

`krkr_paths.cppinc` 在 TVP 的插件 `V2Link` 边界取得官方 exporter，验证原生字体方法后，
把 `mapPrerenderedFont` 请求转到同一字体实例的原生 `unmapPrerenderedFont`。引擎随即
使用实时字体路径，由通用字体钩子应用当前配置。

原生方法名是 TJS 脚本契约的一部分。适配器保留 `mapPrerenderedFont` 名称和松散、
归档 TFT 的可见性，避免脚本报“成员不存在”或字体资源缺失。旧版名称修改与文件隐藏
入口保留为兼容内部调用的透传函数。

## 配置

配置文件使用 `[FontEngines]` 下的以下键；注意 INI 总开关键是 `EnableKrkr`：

```ini
EnableKrkr=0
KrkrDisablePrerenderedFonts=1
KrkrNativeDispatchMode=0
```

| 模式 | 行为 |
| --- | --- |
| `0` 自动 | 只对有版本信息的 TVP 1.4.x 尝试原生分派；旧版、未来版本、未知版本保留原生 map/unmap 与 TFT |
| `1` 强制尝试 | 跳过版本门控，仍验证身份、exporter、原生方法和调用约定 |
| `2` 禁用 | 保留引擎原生字体分派 |

模式 1 适用于已确认接口兼容但版本信息不满足自动模式的程序。它不会为未知接口创造
兼容性。超出 0–2 的配置恢复为自动模式。

绑定还要求 `[Font] Enable=1`、`EnableFaceNameReplace=1`、非空 `FaceName`，以及
`EnableKrkr=1`、`KrkrDisablePrerenderedFonts=1`。关掉这些选项后，已安装的分派钩子
也会透传原请求。

## 身份、版本与接口能力

1. 主模块包含 ANSI 或 Unicode `mapPrerenderedFont`；该标记只用于身份探测，不写入映像。
2. 自动模式检查主模块 `VS_FIXEDFILEINFO` 的版本；仅 1.4.x 进入后续接口绑定。
3. `GetProcAddress` 查询插件 `V2Link` 时分配固定包装入口，保持每个插件原入口独立。
4. exporter 的查询函数及返回的 TJS 辅助函数必须属于主模块。
5. 从全局 `Font` 类取得 map/unmap 对象，使用引擎自身创建的原生方法对象验证两者的
   FuncCall 入口，而不假设私有对象偏移或脚本 dispatcher 布局。
6. 验证通过后在独立 Detours 事务中绑定原生 FuncCall。方法对象引用随进程存活；
   DLL detach 阶段不调用引擎或等待线程。

`.xp3`、`.tft`、路径名和单个扩展名不用于独立确认引擎或 ABI。

## ABI 与分派范围

x86 在受控的 exporter 查询边界测量栈清理方式，分别使用 stdcall/cdecl 虚接口包装。
导出的辅助函数继续使用各自的 SDK 约定。x64 使用平台统一调用约定。

只重定向绑定的 map 对象、空 member、有效字体实例且至少一个参数的原生调用。unmap
使用原 flags、result 和字体实例，以原生逻辑清理映射及缓存并返回结果。其他成员调用、
其他对象和不满足条件的请求继续执行原函数。

插件包装槽最多 16 个，接口绑定最多尝试 32 次。槽耗尽、接口缺失、版本不匹配或
Detours 失败均保留原入口，输出有上限的诊断日志。

## 文件边界

`KrkrShouldHidePrerenderedFontW` 始终返回 false。TFT 的打开、属性与目录搜索由 Nepgear
原有文件钩子和 VFS 决定，不增加第二套文件 Detours。字体分派适配可以处理由松散文件
或 XP3 资源触发的映射调用，但不修改资源本身。

## 验证

原生接口夹具覆盖 x86 stdcall/cdecl、x64、map 转同实例 unmap、原参数和 result、栈平衡、
无关调用透传、模式 2、TVP 1.3 自动回退、未知版本自动回退与模式 1 显式尝试。另用
文件/GDI 探针检查成员名保持原样和 TFT 仍可见。执行方法见仓库根目录 `tests/README.md`。

上述验证使用合成 TVP exporter 与真实 Nepgear DLL，不代表所有 KRKR 游戏渲染实测。
