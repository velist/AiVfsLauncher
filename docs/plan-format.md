# 映射清单格式（vfs-plan.json，schemaVersion 1）

主程序算好"哪些目录按什么顺序叠到哪里"，写成这份 JSON 交给启动器。启动器只照单执行，不做冲突裁决。

- 编码 UTF-8（可带 BOM）。所有路径必须是**绝对路径**，不能含 `..` 段。
- **未知字段一律报错**：防止拼错的字段被静默忽略。
- 字段名区分大小写。

```json
{
  "schemaVersion": 1,
  "instanceName": "skyrimse_default",
  "logLevel": "warning",
  "logFile": "D:\\UEModManager\\logs\\vfs-skyrimse.log",
  "crashDumpPath": "D:\\UEModManager\\dumps",
  "mappings": [
    { "type": "directory", "source": "D:\\Repo\\SkyUI", "destination": "C:\\Games\\Skyrim\\Data" },
    { "type": "directory", "source": "D:\\Repo\\USSEP", "destination": "C:\\Games\\Skyrim\\Data" },
    { "type": "file", "source": "D:\\Profiles\\默认\\plugins.txt",
      "destination": "C:\\Users\\me\\AppData\\Local\\Skyrim Special Edition\\plugins.txt" },
    { "type": "directory", "source": "D:\\Profiles\\默认\\overwrite",
      "destination": "C:\\Games\\Skyrim\\Data", "createTarget": true }
  ],
  "skipFileSuffixes": [".mohidden"],
  "skipDirectories": [],
  "blacklistExecutables": [],
  "forceLoadLibraries": [],
  "launch": {
    "executable": "C:\\Games\\Skyrim\\skse64_loader.exe",
    "arguments": "",
    "workingDirectory": "C:\\Games\\Skyrim"
  }
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `schemaVersion` | 是 | 固定为 `1`。不匹配时只报这一条错并停止 |
| `instanceName` | 是 | 1–48 个 `A-Z a-z 0-9 _ -`。会成为共享内存名（启动器自动加 `aivfs_` 前缀）。同时运行的两个实例不能同名，建议用"游戏_方案编号" |
| `logLevel` | 否 | `debug` / `info` / `warning`（默认）/ `error` |
| `logFile` | 否 | usvfs 日志写到这里（追加）；不给就不写 |
| `crashDumpPath` | 否 | 给了就在被挂钩进程崩溃时写 minidump |
| `mappings` | 是 | 至少一条。**顺序即优先级：后面的覆盖前面的同名文件**（与 MO2 一致） |
| `mappings[].type` | 是 | `directory`（递归链接整个目录）或 `file` |
| `mappings[].source` | 是 | 真实存在的目录或文件 |
| `mappings[].destination` | 是 | 进程"看到"它的位置；可以不存在 |
| `mappings[].createTarget` | 否 | 仅目录。进程在 destination 下**新建**的文件实际写到 source（MO2 的 Overwrite）。同一 destination 只能有一条 |
| `skipFileSuffixes` | 否 | 这些后缀的文件不参与映射（如 `.mohidden`） |
| `skipDirectories` | 否 | 这些目录名不参与映射 |
| `blacklistExecutables` | 否 | 这些可执行文件被拉起时不注入（如崩溃上报器） |
| `forceLoadLibraries` | 否 | `{ "process": "SkyrimSE.exe", "library": "<绝对路径>.dll" }`：在指定进程里强制加载 DLL |
| `launch.executable` | 是 | 要启动的程序 |
| `launch.arguments` | 否 | 原样拼到命令行后面（调用方负责加引号） |
| `launch.workingDirectory` | 否 | 缺省为可执行文件所在目录 |

## 命令行

```
AiVfsLauncher run      --plan <vfs-plan.json> [--report <report.json>]
AiVfsLauncher validate --plan <vfs-plan.json> [--report <report.json>]
AiVfsLauncher version
```

- `run`：建虚拟文件系统 → 链接 → 启动 → **等整棵进程树退出**（加载器拉起的游戏本体也算）→ 写报告。
- `validate`：只检查清单格式与路径，并确认 usvfs DLL 能加载，不启动任何进程。
- 不给 `--report` 时报告打印到标准输出。

## 报告（report.json）

```json
{
  "schemaVersion": 1,
  "launcherVersion": "0.1.0",
  "usvfsVersion": "0.5.7.2",
  "status": "ok",
  "errorCode": "none",
  "message": "",
  "win32Error": 0,
  "suspectedAntivirus": false,
  "planErrors": [ { "field": "mappings[0].source", "message": "目录不存在" } ],
  "mappingsApplied": 4,
  "mappingFailures": [ { "index": 2, "win32Error": 5 } ],
  "rootProcessId": 1234,
  "rootExitCode": 0,
  "processIds": [1234, 5678],
  "startedAtUtc": "2026-10-31T09:00:00.000Z",
  "endedAtUtc": "2026-10-31T10:30:00.000Z"
}
```

报告先写临时文件再替换，读到的一定是完整的。

## 退出码

| 值 | errorCode | 含义 | 主程序建议处理 |
|---|---|---|---|
| 0 | `none` | 正常；游戏自己的退出码见 `rootExitCode` | — |
| 2 | `invalidArguments` | 命令行参数错误 | 程序缺陷 |
| 3 | `planUnreadable` | 读不到清单文件 | 程序缺陷 / 磁盘问题 |
| 4 | `invalidPlan` | 清单格式错误，见 `planErrors` | 程序缺陷 |
| 5 | `pathCheckFailed` | 清单里有路径不存在，见 `planErrors` | 提示"某个 MOD 文件丢了"，引导修复仓库 |
| 6 | `usvfsUnavailable` | usvfs DLL 加载失败 | 多半是杀软隔离或缺 VC++ 运行库 → 引导加白名单/修复安装；可改用真文件部署 |
| 7 | `vfsCreateFailed` | 创建虚拟文件系统失败 | 改用真文件部署 |
| 8 | `mappingFailed` | 有映射建立失败，见 `mappingFailures`；**不会带病启动** | 改用真文件部署 |
| 9 | `processCreateFailed` | 进程没启动起来 | 看 `suspectedAntivirus` 决定提示 |
| 10 | `internal` | 启动器内部异常 | 程序缺陷 |
| 11 | `injectionFailed` | 进程建起来了但注入失败（usvfs 会结束它） | 多半是杀软拦截 → 引导加白名单；可改用真文件部署 |

退出码只会在末尾追加，已有取值不会改。

## 注意

- **x64 目标**：当前只链接 64 位 usvfs。上古卷轴5 SE、赛博朋克2077 都是 64 位。被挂钩进程再拉起 32 位程序时，usvfs 会通过随附的 `usvfs_proxy_x86.exe` 注入。
- **钩子生效前就加载的文件**（代理 DLL、脚本扩展的加载器本体等）在虚拟文件系统里看不见，必须由主程序真实部署到游戏目录。
- **从 Steam 直接启动**的游戏不在虚拟文件系统里。若游戏通过 Steam 重新拉起自己（steam_api 的 restart 机制），新进程同样不在里面——需要游戏目录有 `steam_appid.txt` 或使用对应的加载器。
- 依赖 **VC++ 2015–2022 x64 运行库**（usvfs 本身需要，且会被注入到游戏进程里）。
