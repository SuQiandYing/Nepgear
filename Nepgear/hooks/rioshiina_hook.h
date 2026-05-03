#pragma once

namespace Hooks {
    // 安装 RioShiina 引擎 Hook (注册表处理 + 文件重定向)
    void InstallRioShiinaHook();
    // 确保解包后扫到特征
    void EnsureRioShiinaHooked();
}
