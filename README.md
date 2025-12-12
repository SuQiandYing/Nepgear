# Nepgear

**Nepgear** 是一个专为日语 Galgame（视觉小说）设计的通用 DLL 劫持与 Hook 框架。

通过劫持系统 DLL（默认 `winmm.dll`），Nepgear 可以自动随游戏启动，并提供区域模拟、字体替换、文件重定向等强大功能。

## ✨ 主要功能

*   **DLL 劫持加载**：利用 `winmm.dll` 代理技术，无需修改游戏原有可执行文件即可自动加载。
*   **区域模拟 (Locale Emulator)**：
    *   内置区域模拟功能，可在非日语系统上直接运行 Shift-JIS 编码的日语游戏。
    *   支持模拟 Shift-JIS (932) 或 GBK (936) 环境。
*   **高级字体挂钩 (Font Hook)**：
    *   动态替换游戏字体（默认 `galgame_cnjp.ttf`）。
    *   支持自定义字体大小缩放（宽高比调整）。
    *   支持字体粗细调整。
    *   强制指定字符集（解决乱码问题）。
*   **窗口标题修改**：自定义游戏窗口标题，支持 Unicode。
*   **虚拟文件系统 (VFS) & 文件重定向**：
    *   将游戏读取的文件重定向到外部文件夹（如 `Nepgear/`）。
    *   支持读取打包的 `.chs` 资源包（类似封包汉化）。
    *   优先读取外部文件，方便替换游戏资源（图片、脚本等）。

## 🛠️ 构建指南

### 环境要求
*   Visual Studio 2022 (v143 工具集)
*   Windows SDK

### 编译步骤
1.  使用 Visual Studio 2022 打开 `Nepgear.sln`。
2.  选择构建配置为 `Release` / `x86` (大多数 Galgame 为 32 位程序)。
3.  点击 **生成解决方案 (Build Solution)**。
4.  编译完成后：
    *   在 `Release/` 目录下会生成 `Nepgear.dll`。
    *   在 `Packer/Release/` (或类似路径) 下会生成 `Packer.exe`。

## 📦 安装与使用

### 1. 部署文件
将以下文件复制到游戏根目录（即游戏主 EXE 所在的目录）：

1.  **Nepgear.dll** -> 重命名为 **`winmm.dll`**。
    *   *原理：游戏启动时会尝试加载系统的 winmm.dll，重命名后游戏会优先加载我们的 DLL，Nepgear 会自动将原有函数调用转发给系统真正的 winmm.dll。*
2.  **Nepgear.ini** -> 配置文件。
3.  **galgame_cnjp.ttf** -> 你想要替换的字体文件（文件名需与 ini 中配置一致）。

### 2. 配置文件说明 (Nepgear.ini)
配置文件需保存为 **UTF-16 LE** 编码。

```ini
[System]
Enable=1                ; 启用/禁用插件

[Font]
Enable=1                ; 启用字体 Hook
FileName=galgame_cnjp.ttf ; 字体文件名 (需放在游戏目录)
FaceName=galgame        ; 字体名称 (通常不需要修改，除非游戏检查字体名)

; 字符集强制替换 (128=Shift-JIS, 134=GB2312)
Charset=128
EnableCharsetReplace=1

; 字体宽高缩放 (1.0 为原始比例)
EnableHeightScale=1
HeightScale=1.0
EnableWidthScale=1
WidthScale=1.0

; 字体粗细 (0=默认, 400=正常, 700=粗体)
EnableWeight=1
Weight=0

[Window]
Enable=1                ; 启用窗口标题修改
Title=自定义游戏标题     ; 新的窗口标题

[FileRedirect]
Enable=1                ; 启用文件重定向
Folder=Nepgear          ; 重定向读取的文件夹名称
ArchiveFile=Nepgear.chs ; 打包资源文件名 (优先级低于文件夹)

[LocaleEmulator]
Enable=1                ; 启用区域模拟
CodePage=932            ; 模拟代码页 (932=日语, 936=中文)
LocaleID=1041           ; 区域 ID (1041=日语, 2052=中文)
Charset=128             ; 默认字符集
Timezone=Tokyo Standard Time ; 模拟时区
```

### 3. 使用 Packer 打包资源
Nepgear 配套了一个简单的打包工具 `Packer.exe`，用于将翻译资源打包成 `.chs` 单个文件。

**使用方法：**
1.  将需要替换的游戏资源（图片、脚本等）放入一个文件夹，保持原有的目录结构。
2.  将该文件夹直接**拖拽**到 `Packer.exe` 图标上。
3.  程序会自动在同级目录生成同名的 `.chs` 文件（例如拖拽 `Nepgear` 文件夹 -> 生成 `Nepgear.chs`）。
4.  将生成的 `.chs` 文件放入游戏目录，并在 `Nepgear.ini` 中配置 `ArchiveFile=xxx.chs`。

## ⚠️ 注意事项
*   **编码问题**：`Nepgear.ini` 必须是 **UTF-16 LE** (带 BOM) 编码，否则可能无法读取。
*   **32位/64位**：请根据游戏程序的架构编译对应的 DLL。绝大多数老游戏和 Galgame 都是 32 位 (x86)。
*   **反作弊/保护**：部分带有强力 DRM 或反作弊系统的游戏可能会阻止 DLL 劫持。

## 🤝 贡献
欢迎提交 Issue 或 Pull Request 来改进本项目。

## 📄 许可证
[MIT License](LICENSE) (如有)
