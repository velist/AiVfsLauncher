# AiVfsLauncher

爱酱MOD管理器的**虚拟文件启动器**：用 [MO2 的 usvfs](https://github.com/ModOrganizer2/usvfs) 让游戏"看到" MOD 文件，而游戏目录一个文件都不改。关掉游戏，一切恢复原样。

- 主程序（爱酱MOD管理器）算好映射清单 `vfs-plan.json`，启动器照单执行：建虚拟文件系统 → 按顺序链接 → 挂钩启动游戏 → 等整棵进程树退出 → 写报告。
- 启动器是独立进程、单独以 **GPL-3.0** 开源；主程序只通过命令行和 JSON 文件与它通信。
- 清单、报告与退出码的完整说明见 [docs/plan-format.md](docs/plan-format.md)。

## 能做什么

| | |
|---|---|
| 多个 MOD 叠加到同一目录 | 清单里后面的覆盖前面的同名文件（与 MO2 相同） |
| 游戏/工具新建的文件 | 写进指定的"新增文件区"，不落到游戏目录 |
| 加载器拉起游戏本体（如 `skse64_loader.exe`） | 用作业对象跟踪整棵进程树，最后一个进程退出前不撤掉虚拟文件系统 |
| 单文件映射 | 例如把方案里的 `plugins.txt` 映射到 `%LOCALAPPDATA%` |
| 出错时 | 报告给出错误码、Win32 错误码、是否疑似杀毒软件拦截；映射建失败时**不启动游戏** |

## 构建

需要 Visual Studio 2022（"使用 C++ 的桌面开发"，自带 CMake）。依赖在配置时按固定版本下载并校验 SHA256：usvfs 0.5.7.2（MO2 官方发布包）、nlohmann/json 3.12.0、doctest 2.4.12。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

产物在 `build/Release/`：`AiVfsLauncher.exe` 与 usvfs 的 `usvfs_x64.dll`、`usvfs_x86.dll`、`usvfs_proxy_x64.exe`、`usvfs_proxy_x86.exe`，这五个文件要放在同一目录。

`cmake --install build --config Release --prefix dist` 生成发布目录，额外附带 VC++ 运行库 DLL（应用本地部署），没装运行库的电脑上启动器也能运行。注意 usvfs 会被注入游戏进程，游戏进程里仍依赖系统的 VC++ 运行库——主流游戏本身就要求安装它。

## 发布

推送 `v*` 标签后 CI 自动构建、测试并创建 GitHub Release，附 `AiVfsLauncher-<版本>-x64.zip` 及其 SHA256。爱酱MOD管理器的安装包构建脚本按固定版本和哈希下载使用。

## 测试

- `aivfs_tests`：清单解析、报告序列化的单元测试。
- `aivfs_e2e`：真实建立虚拟文件系统并挂钩启动测试程序 `aivfs_probe`（一个"假游戏"），在带空格和中文的路径下检查：叠加可见、覆盖顺序、新建文件被改写到新增文件区、游戏目录零改动、加载器退出后仍等待子进程。

## 已知限制

- usvfs 官方说明自己处于 alpha 状态；它靠 API 钩子和 DLL 注入工作，**可能被杀毒软件误报或拦截**。
- 钩子生效前就加载的文件（代理 DLL、脚本扩展加载器本体）必须真实放进游戏目录，这由主程序负责。
- 只做 x64。

## 许可

GPL-3.0-or-later，见 [LICENSE](LICENSE)。usvfs 版权归其作者，同样以 GPL-3.0 发布，源码见 https://github.com/ModOrganizer2/usvfs 。
