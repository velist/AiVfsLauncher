#pragma once

#include "plan.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aivfs
{
// 启动器的退出码就是这个枚举的值；主程序据此决定给玩家看什么提示。
// 只能在末尾追加，已有取值不能改。
enum class ErrorCode : int
{
  None = 0,
  InvalidArguments = 2,
  PlanUnreadable = 3,
  InvalidPlan = 4,
  PathCheckFailed = 5,
  UsvfsUnavailable = 6,
  VfsCreateFailed = 7,
  MappingFailed = 8,
  ProcessCreateFailed = 9,
  Internal = 10,
  // 进程建起来了但 usvfs 注入失败（usvfs 会把它杀掉）——最常见的原因是杀毒软件拦截。
  InjectionFailed = 11
};

const char* ErrorCodeName(ErrorCode code);

struct MappingFailure
{
  size_t index = 0;
  uint32_t win32Error = 0;
};

struct Report
{
  // false 表示游戏还在运行：启动成功后会先写一份 status=running 的报告，结束时再覆盖。
  bool finished = false;
  ErrorCode errorCode = ErrorCode::None;
  std::string message;
  uint32_t win32Error = 0;
  // 拒绝访问、"含有病毒"之类的错误码，多半是杀毒软件拦了注入或隔离了 DLL。
  bool suspectedAntivirus = false;
  std::vector<PlanError> planErrors;
  size_t mappingsApplied = 0;
  std::vector<MappingFailure> mappingFailures;
  std::optional<uint32_t> rootProcessId;
  std::optional<uint32_t> rootExitCode;
  // 运行期间出现过的整棵进程树（例如 SKSE 加载器再拉起游戏本体），按首次出现排序。
  std::vector<uint32_t> processIds;
  std::string startedAtUtc;
  std::string endedAtUtc;
  std::string usvfsVersion;
};

bool IsLikelyAntivirusError(uint32_t win32Error);

std::string SerializeReport(const Report& report);

// 先写临时文件再替换，主程序不会读到写了一半的报告。
bool WriteReportFile(const std::wstring& path, const Report& report);

std::string UtcNowIso8601();
} // namespace aivfs
