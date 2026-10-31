// 端到端测试：真的建虚拟文件系统、真的挂钩启动 aivfs_probe，检查它"看到"的文件。
//
//   aivfs_e2e <AiVfsLauncher.exe> <aivfs_probe.exe>
//
// 测试目录故意带空格和中文：玩家的游戏和 MOD 仓库路径里这两样都很常见。

#include "text.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
namespace fs = std::filesystem;
using aivfs::WideToUtf8;
using nlohmann::json;

int g_failures = 0;

void Check(bool condition, const std::string& what)
{
  std::printf("%s %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
  if (!condition) {
    ++g_failures;
  }
}

void WriteText(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string ReadText(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string U8(const fs::path& path)
{
  return WideToUtf8(path.wstring());
}

std::wstring Q(const fs::path& path)
{
  return L"\"" + path.wstring() + L"\"";
}

// 返回启动器退出码；超时返回 -1。
long RunLauncher(const fs::path& launcher, const fs::path& plan, const fs::path& report)
{
  std::wstring commandLine = Q(launcher) + L" run --plan " + Q(plan) + L" --report " + Q(report);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                      &startup, &info)) {
    std::printf("无法启动启动器，错误 %lu\n", GetLastError());
    return -1;
  }
  CloseHandle(info.hThread);
  const DWORD wait = WaitForSingleObject(info.hProcess, 60'000);
  DWORD exitCode = static_cast<DWORD>(-1);
  if (wait == WAIT_OBJECT_0) {
    GetExitCodeProcess(info.hProcess, &exitCode);
  } else {
    TerminateProcess(info.hProcess, 99);
  }
  CloseHandle(info.hProcess);
  return wait == WAIT_OBJECT_0 ? static_cast<long>(exitCode) : -1;
}

json ReadJson(const fs::path& path)
{
  const auto text = ReadText(path);
  return json::parse(text, nullptr, false);
}

void TestVirtualOverlay(const fs::path& root, const fs::path& launcher, const fs::path& probe)
{
  std::printf("\n== 叠加映射、覆盖顺序、新文件改写、等待子进程 ==\n");
  const auto data = root / L"游戏" / L"Data";
  const auto modA = root / L"仓库" / L"模组 A";
  const auto modB = root / L"仓库" / L"模组 B";
  const auto overwrite = root / L"新增文件";
  const auto result = root / L"result.json";
  const auto report = root / L"report.json";

  WriteText(data / L"base.txt", "base");
  WriteText(data / L"a.txt", "original");
  WriteText(modA / L"a.txt", "A");
  WriteText(modA / L"sub" / L"x.txt", "x-from-A");
  WriteText(modB / L"a.txt", "B");
  fs::create_directories(overwrite);

  const std::wstring arguments =
      L"--spawn-detached --out " + Q(result) + L" --list " + Q(data) + L" --list " +
      Q(data / L"sub") + L" --read " + Q(data / L"a.txt") + L" --read " +
      Q(data / L"sub" / L"x.txt") + L" --read " + Q(data / L"base.txt") + L" --write " +
      Q(data / L"new.txt");

  json plan;
  plan["schemaVersion"] = 1;
  plan["instanceName"] = "e2e_" + std::to_string(GetCurrentProcessId());
  plan["logLevel"] = "debug";
  plan["logFile"] = U8(root / L"usvfs.log");
  plan["mappings"] = json::array({
      {{"type", "directory"}, {"source", U8(modA)}, {"destination", U8(data)}},
      {{"type", "directory"}, {"source", U8(modB)}, {"destination", U8(data)}},
      {{"type", "directory"}, {"source", U8(overwrite)}, {"destination", U8(data)},
       {"createTarget", true}},
  });
  plan["launch"] = {{"executable", U8(probe)}, {"arguments", WideToUtf8(arguments)}};
  WriteText(root / L"plan.json", plan.dump(2));

  const long exitCode = RunLauncher(launcher, root / L"plan.json", report);
  Check(exitCode == 0, "启动器退出码为 0（实际 " + std::to_string(exitCode) + "）");

  const auto r = ReadJson(report);
  Check(!r.is_discarded() && r["status"] == "ok", "报告 status=ok");
  if (!r.is_discarded()) {
    std::printf("     报告：errorCode=%s message=%s\n", r["errorCode"].dump().c_str(),
                r["message"].dump().c_str());
    Check(r["mappingsApplied"] == 3, "3 条映射全部建立");
    Check(r["processIds"].size() >= 2, "记录到加载器和子进程两个进程");
  }

  // 子进程延迟 1.5 秒才干活；启动器若没等它，这个文件就不会存在。
  Check(fs::exists(result), "启动器等到了子进程结束（结果文件已写出）");
  const auto p = ReadJson(result);
  if (p.is_discarded()) {
    Check(false, "结果文件可解析");
    return;
  }

  const auto listing = p["lists"][U8(data)];
  auto has = [&](const char* name) {
    return listing.is_array() && std::find(listing.begin(), listing.end(), name) != listing.end();
  };
  Check(has("base.txt"), "游戏原有文件可见");
  Check(has("a.txt") && has("sub"), "MOD 文件与目录可见");
  Check(p["reads"][U8(data / L"a.txt")] == "B", "同名文件后映射者胜出（读到 B）");
  Check(p["reads"][U8(data / L"sub" / L"x.txt")] == "x-from-A", "MOD 子目录文件可读");
  Check(p["reads"][U8(data / L"base.txt")] == "base", "未被覆盖的原文件可读");
  Check(p["writes"][U8(data / L"new.txt")] == true, "游戏内新建文件成功");

  Check(fs::exists(overwrite / L"new.txt"), "新建文件实际落到新增文件区");
  Check(!fs::exists(data / L"new.txt"), "游戏目录里没有真实的新文件");
  Check(ReadText(data / L"a.txt") == "original", "游戏目录原文件未被改动");
  Check(!fs::exists(data / L"sub"), "游戏目录里没有 MOD 目录残留");
}

void TestInvalidPlan(const fs::path& root, const fs::path& launcher)
{
  std::printf("\n== 清单格式错误 ==\n");
  WriteText(root / L"bad-plan.json", R"({ "schemaVersion": 1, "instanceName": "x" })");
  const long exitCode = RunLauncher(launcher, root / L"bad-plan.json", root / L"bad-report.json");
  Check(exitCode == 4, "退出码 4 = invalidPlan（实际 " + std::to_string(exitCode) + "）");
  const auto r = ReadJson(root / L"bad-report.json");
  Check(!r.is_discarded() && r["errorCode"] == "invalidPlan" && !r["planErrors"].empty(),
        "报告列出具体字段错误");
}

void TestMissingSource(const fs::path& root, const fs::path& launcher, const fs::path& probe)
{
  std::printf("\n== 映射源目录不存在 ==\n");
  json plan;
  plan["schemaVersion"] = 1;
  plan["instanceName"] = "missing";
  plan["mappings"] = json::array({{{"type", "directory"},
                                   {"source", U8(root / L"不存在的目录")},
                                   {"destination", U8(root / L"游戏")}}});
  plan["launch"] = {{"executable", U8(probe)}};
  WriteText(root / L"missing-plan.json", plan.dump());
  const long exitCode =
      RunLauncher(launcher, root / L"missing-plan.json", root / L"missing-report.json");
  Check(exitCode == 5, "退出码 5 = pathCheckFailed（实际 " + std::to_string(exitCode) + "）");
  const auto r = ReadJson(root / L"missing-report.json");
  Check(!r.is_discarded() && r["planErrors"][0]["field"] == "mappings[0].source",
        "报告指出是哪条映射");
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
  SetConsoleOutputCP(CP_UTF8);
  if (argc != 3) {
    std::printf("用法：aivfs_e2e <AiVfsLauncher.exe> <aivfs_probe.exe>\n");
    return 2;
  }
  const fs::path launcher = fs::absolute(argv[1]);
  const fs::path probe = fs::absolute(argv[2]);
  const fs::path root =
      fs::temp_directory_path() / (L"aivfs e2e 测试 " + std::to_wstring(GetCurrentProcessId()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  std::printf("测试目录：%s\n", U8(root).c_str());

  TestVirtualOverlay(root, launcher, probe);
  TestInvalidPlan(root, launcher);
  TestMissingSource(root, launcher, probe);

  if (g_failures == 0) {
    fs::remove_all(root, ec);
  } else {
    std::printf("\n保留测试目录供排查（含 usvfs.log）\n");
  }
  std::printf("\n%s：%d 项失败\n", g_failures == 0 ? "通过" : "失败", g_failures);
  return g_failures == 0 ? 0 : 1;
}
