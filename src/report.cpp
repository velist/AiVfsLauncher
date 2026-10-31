#include "report.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace aivfs
{
const char* ErrorCodeName(ErrorCode code)
{
  switch (code) {
  case ErrorCode::None: return "none";
  case ErrorCode::InvalidArguments: return "invalidArguments";
  case ErrorCode::PlanUnreadable: return "planUnreadable";
  case ErrorCode::InvalidPlan: return "invalidPlan";
  case ErrorCode::PathCheckFailed: return "pathCheckFailed";
  case ErrorCode::UsvfsUnavailable: return "usvfsUnavailable";
  case ErrorCode::VfsCreateFailed: return "vfsCreateFailed";
  case ErrorCode::MappingFailed: return "mappingFailed";
  case ErrorCode::ProcessCreateFailed: return "processCreateFailed";
  case ErrorCode::Internal: return "internal";
  case ErrorCode::InjectionFailed: return "injectionFailed";
  }
  return "unknown";
}

bool IsLikelyAntivirusError(uint32_t win32Error)
{
  switch (win32Error) {
  case ERROR_ACCESS_DENIED:
  case ERROR_VIRUS_INFECTED:
  case ERROR_VIRUS_DELETED:
  case ERROR_MOD_NOT_FOUND:  // DLL 被隔离后最常见的表现
    return true;
  default:
    return false;
  }
}

std::string SerializeReport(const Report& report)
{
  using nlohmann::json;
  json j;
  j["schemaVersion"] = 1;
  j["launcherVersion"] = AIVFS_VERSION;
  j["usvfsVersion"] = report.usvfsVersion;
  j["status"] = report.errorCode == ErrorCode::None ? "ok" : "error";
  j["errorCode"] = ErrorCodeName(report.errorCode);
  j["message"] = report.message;
  j["win32Error"] = report.win32Error;
  j["suspectedAntivirus"] = report.suspectedAntivirus;

  json planErrors = json::array();
  for (const auto& e : report.planErrors) {
    planErrors.push_back({{"field", e.field}, {"message", e.message}});
  }
  j["planErrors"] = std::move(planErrors);

  j["mappingsApplied"] = report.mappingsApplied;
  json failures = json::array();
  for (const auto& f : report.mappingFailures) {
    failures.push_back({{"index", f.index}, {"win32Error", f.win32Error}});
  }
  j["mappingFailures"] = std::move(failures);

  j["rootProcessId"] = report.rootProcessId ? json(*report.rootProcessId) : json(nullptr);
  j["rootExitCode"] = report.rootExitCode ? json(*report.rootExitCode) : json(nullptr);
  j["processIds"] = report.processIds;
  j["startedAtUtc"] = report.startedAtUtc;
  j["endedAtUtc"] = report.endedAtUtc;
  return j.dump(2);
}

bool WriteReportFile(const std::wstring& path, const Report& report)
{
  const std::wstring temp = path + L".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    const auto text = SerializeReport(report);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
      return false;
    }
  }
  return MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::string UtcNowIso8601()
{
  SYSTEMTIME t{};
  GetSystemTime(&t);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth,
                t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  return buffer;
}
} // namespace aivfs
