// AiVfsLauncher —— 爱酱MOD管理器的虚拟文件启动器。
//
// 用法：
//   AiVfsLauncher run      --plan <vfs-plan.json> [--report <report.json>]
//   AiVfsLauncher validate --plan <vfs-plan.json> [--report <report.json>]
//   AiVfsLauncher version
//
// 退出码见 report.hpp 的 ErrorCode；游戏自己的退出码写在报告的 rootExitCode 里。
// 没给 --report 时报告打印到标准输出。

#include "plan.hpp"
#include "report.hpp"
#include "text.hpp"
#include "vfs_session.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>

namespace
{
using namespace aivfs;

struct Options
{
  std::wstring command;
  std::wstring planPath;
  std::wstring reportPath;
};

std::optional<Options> ParseArguments(int argc, wchar_t** argv, std::string& error)
{
  if (argc < 2) {
    error = "缺少命令：run、validate 或 version";
    return std::nullopt;
  }
  Options options;
  options.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if ((arg == L"--plan" || arg == L"--report") && i + 1 < argc) {
      (arg == L"--plan" ? options.planPath : options.reportPath) = argv[++i];
    } else {
      error = "无法识别的参数：" + WideToUtf8(arg);
      return std::nullopt;
    }
  }
  if (options.command == L"version") {
    return options;
  }
  if (options.command != L"run" && options.command != L"validate") {
    error = "未知命令：" + WideToUtf8(options.command);
    return std::nullopt;
  }
  if (options.planPath.empty()) {
    error = "缺少 --plan";
    return std::nullopt;
  }
  return options;
}

std::optional<std::string> ReadFile(const std::wstring& path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  // 主程序（.NET）写 UTF-8 时可能带 BOM。
  if (content.rfind("\xEF\xBB\xBF", 0) == 0) {
    content.erase(0, 3);
  }
  return content;
}

void WriteStdout(const std::string& text)
{
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

int Finish(const Options* options, Report& report)
{
  report.endedAtUtc = UtcNowIso8601();
  if (options && !options->reportPath.empty()) {
    if (!WriteReportFile(options->reportPath, report)) {
      std::fprintf(stderr, "无法写入报告文件\n");
    }
  } else {
    WriteStdout(SerializeReport(report));
  }
  return static_cast<int>(report.errorCode);
}

int PrintVersion()
{
  nlohmann::json j;
  j["launcherVersion"] = AIVFS_VERSION;
  j["planSchemaVersion"] = kPlanSchemaVersion;
  Report probe;
  j["usvfsVersion"] =
      VfsSession::LoadUsvfs(probe) ? nlohmann::json(probe.usvfsVersion) : nlohmann::json(nullptr);
  WriteStdout(j.dump(2));
  return 0;
}

int Execute(const Options& options, Report& report)
{
  const auto text = ReadFile(options.planPath);
  if (!text) {
    report.errorCode = ErrorCode::PlanUnreadable;
    report.win32Error = GetLastError();
    report.message = "无法读取清单文件";
    return Finish(&options, report);
  }

  auto parsed = ParsePlan(*text);
  if (!parsed.plan) {
    report.errorCode = ErrorCode::InvalidPlan;
    report.message = "清单格式不正确";
    report.planErrors = std::move(parsed.errors);
    return Finish(&options, report);
  }

  report.planErrors = CheckPlanPaths(*parsed.plan);
  if (!report.planErrors.empty()) {
    report.errorCode = ErrorCode::PathCheckFailed;
    report.message = "清单里有路径不存在";
    return Finish(&options, report);
  }

  if (!VfsSession::LoadUsvfs(report)) {
    return Finish(&options, report);
  }
  if (options.command == L"validate") {
    report.message = "清单有效";
    return Finish(&options, report);
  }

  {
    VfsSession session;
    session.Run(*parsed.plan, report);
  }
  return Finish(&options, report);
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
  Report report;
  report.startedAtUtc = UtcNowIso8601();

  std::string error;
  const auto options = ParseArguments(argc, argv, error);
  if (!options) {
    report.errorCode = ErrorCode::InvalidArguments;
    report.message = error;
    return Finish(nullptr, report);
  }
  if (options->command == L"version") {
    return PrintVersion();
  }

  try {
    return Execute(*options, report);
  } catch (const std::exception& e) {
    report.errorCode = ErrorCode::Internal;
    report.message = e.what();
    return Finish(&*options, report);
  }
}
