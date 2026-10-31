#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aivfs
{
// 映射清单（vfs-plan.json）的内存表示。格式见 docs/plan-format.md。
// 主程序负责算出"哪些目录按什么顺序叠到哪里"，启动器只照单执行，不做任何裁决。

enum class MappingType
{
  Directory,
  File
};

struct Mapping
{
  MappingType type = MappingType::Directory;
  std::wstring source;
  std::wstring destination;
  // 只用于目录：进程在 destination 下新建的文件改写到 source（MO2 的 Overwrite）。
  bool createTarget = false;
};

struct ForceLoad
{
  std::wstring process;
  std::wstring library;
};

struct LaunchSpec
{
  std::wstring executable;
  std::wstring arguments;
  std::wstring workingDirectory;
};

enum class LogLevel
{
  Debug,
  Info,
  Warning,
  Error
};

struct Plan
{
  int schemaVersion = 0;
  std::string instanceName;
  LogLevel logLevel = LogLevel::Warning;
  std::wstring logFile;
  std::wstring crashDumpPath;
  // 顺序即优先级：后面的映射覆盖前面的同名文件。
  std::vector<Mapping> mappings;
  std::vector<std::wstring> skipFileSuffixes;
  std::vector<std::wstring> skipDirectories;
  std::vector<std::wstring> blacklistExecutables;
  std::vector<ForceLoad> forceLoadLibraries;
  LaunchSpec launch;
};

struct PlanError
{
  // JSON 路径，如 "mappings[2].source"；整份文档的问题用 "$"。
  std::string field;
  std::string message;
};

struct ParseResult
{
  std::optional<Plan> plan;
  std::vector<PlanError> errors;
};

inline constexpr int kPlanSchemaVersion = 1;

// 纯函数：只看结构与取值，不碰文件系统。
ParseResult ParsePlan(std::string_view json);

// 检查路径在磁盘上是否真实存在、类型是否对得上。
std::vector<PlanError> CheckPlanPaths(const Plan& plan);

// usvfs 要求的命令行：带引号的可执行路径 + 参数。
std::wstring BuildCommandLine(const LaunchSpec& launch);
} // namespace aivfs
