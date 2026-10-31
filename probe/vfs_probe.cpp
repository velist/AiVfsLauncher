// 端到端测试用的"假游戏"：在虚拟文件系统里列目录、读文件、写文件，把看到的结果写成 JSON。
//
//   aivfs_probe --out <result.json> [--list <dir>]... [--read <file>]... [--write <file>]...
//               [--delay-ms <n>] [--spawn-detached]
//
// --spawn-detached 模拟 SKSE 加载器：自己不干活，拉起一个子进程后立刻退出，
// 用来验证启动器会等到子进程（游戏本体）结束才撤掉虚拟文件系统。

#include "text.hpp"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using aivfs::WideToUtf8;

std::wstring Quote(const std::wstring& arg)
{
  return L"\"" + arg + L"\"";
}

int SpawnDetachedChild(int argc, wchar_t** argv)
{
  std::wstring commandLine = Quote(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (std::wstring(argv[i]) == L"--spawn-detached") {
      continue;
    }
    commandLine += L" " + Quote(argv[i]);
  }
  commandLine += L" --delay-ms 1500";

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                      &startup, &info)) {
    return 20;
  }
  CloseHandle(info.hThread);
  CloseHandle(info.hProcess);
  return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
  std::wstring out;
  std::vector<std::wstring> lists, reads, writes;
  DWORD delayMs = 0;
  bool spawn = false;

  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    const bool hasValue = i + 1 < argc;
    if (arg == L"--out" && hasValue) out = argv[++i];
    else if (arg == L"--list" && hasValue) lists.emplace_back(argv[++i]);
    else if (arg == L"--read" && hasValue) reads.emplace_back(argv[++i]);
    else if (arg == L"--write" && hasValue) writes.emplace_back(argv[++i]);
    else if (arg == L"--delay-ms" && hasValue) delayMs = std::stoul(argv[++i]);
    else if (arg == L"--spawn-detached") spawn = true;
    else return 10;
  }
  if (out.empty()) {
    return 11;
  }
  if (spawn) {
    return SpawnDetachedChild(argc, argv);
  }
  Sleep(delayMs);

  nlohmann::json result;
  result["pid"] = GetCurrentProcessId();

  nlohmann::json listed = nlohmann::json::object();
  for (const auto& dir : lists) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::path(dir), ec)) {
      names.push_back(WideToUtf8(entry.path().filename().wstring()));
    }
    std::sort(names.begin(), names.end());
    listed[WideToUtf8(dir)] = ec ? nlohmann::json(nullptr) : nlohmann::json(names);
  }
  result["lists"] = std::move(listed);

  nlohmann::json read = nlohmann::json::object();
  for (const auto& file : reads) {
    std::ifstream in(fs::path(file), std::ios::binary);
    read[WideToUtf8(file)] =
        in ? nlohmann::json(std::string((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>()))
           : nlohmann::json(nullptr);
  }
  result["reads"] = std::move(read);

  nlohmann::json written = nlohmann::json::object();
  for (const auto& file : writes) {
    std::ofstream o(fs::path(file), std::ios::binary | std::ios::trunc);
    o << "written-by-probe";
    written[WideToUtf8(file)] = static_cast<bool>(o);
  }
  result["writes"] = std::move(written);

  std::ofstream o(fs::path(out), std::ios::binary | std::ios::trunc);
  o << result.dump(2);
  return o ? 0 : 12;
}
