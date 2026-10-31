#include "vfs_session.hpp"

#include "skeleton.hpp"
#include "text.hpp"

#include <windows.h>

#include <usvfs/usvfs.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace aivfs
{
namespace
{
namespace fs = std::filesystem;

::LogLevel ToUsvfsLogLevel(LogLevel level)
{
  switch (level) {
  case LogLevel::Debug: return ::LogLevel::Debug;
  case LogLevel::Info: return ::LogLevel::Info;
  case LogLevel::Warning: return ::LogLevel::Warning;
  case LogLevel::Error: return ::LogLevel::Error;
  }
  return ::LogLevel::Warning;
}

fs::path ExecutableDirectory()
{
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      buffer.resize(length);
      return fs::path(buffer).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

// 实例名会成为共享内存名；加上前缀避免与 MO2 的实例撞名。
std::string SharedMemoryName(const std::string& instanceName)
{
  return "aivfs_" + instanceName;
}
} // namespace

VfsSession::~VfsSession()
{
  // 先停日志线程：它在轮询 usvfs 的日志缓冲区，断开之后再读不安全。
  StopLogPump();
  if (connected_) {
    usvfsDisconnectVFS();
  }
  if (job_ != nullptr) {
    CloseHandle(job_);
  }
  if (!skeletonRoot_.empty()) {
    std::error_code ec;
    fs::remove_all(fs::path(skeletonRoot_), ec);
  }
}

bool VfsSession::LoadUsvfs(Report& report)
{
  const auto dll = ExecutableDirectory() / L"usvfs_x64.dll";
  if (LoadLibraryW(dll.c_str()) == nullptr) {
    const DWORD error = GetLastError();
    report.errorCode = ErrorCode::UsvfsUnavailable;
    report.win32Error = error;
    report.suspectedAntivirus = IsLikelyAntivirusError(error);
    report.message = "无法加载 " + WideToUtf8(dll.wstring()) +
                     "；文件缺失、被杀毒软件隔离，或缺少 VC++ 2015-2022 运行库";
    return false;
  }
  report.usvfsVersion = usvfsVersionString();
  return true;
}

bool VfsSession::Run(const Plan& plan, Report& report,
                     const std::function<void(const Report&)>& onStarted)
{
  if (!Create(plan, report) || !ApplyMappings(plan, report)) {
    return false;
  }
  void* process = nullptr;
  if (!Launch(plan, report, process)) {
    return false;
  }
  if (onStarted) {
    onStarted(report);
  }
  WaitForProcessTree(process, report);
  CloseHandle(process);
  return true;
}

bool VfsSession::Create(const Plan& plan, Report& report)
{
  if (!plan.logFile.empty()) {
    StartLogPump(plan.logFile);
  }

  usvfsParameters* params = usvfsCreateParameters();
  usvfsSetInstanceName(params, SharedMemoryName(plan.instanceName).c_str());
  usvfsSetDebugMode(params, FALSE);
  usvfsSetLogLevel(params, ToUsvfsLogLevel(plan.logLevel));
  if (plan.crashDumpPath.empty()) {
    usvfsSetCrashDumpType(params, CrashDumpsType::None);
  } else {
    usvfsSetCrashDumpType(params, CrashDumpsType::Mini);
    usvfsSetCrashDumpPath(params, WideToUtf8(plan.crashDumpPath).c_str());
  }
  usvfsSetProcessDelay(params, 0);

  usvfsInitLogging(false);
  const BOOL created = usvfsCreateVFS(params);
  usvfsFreeParameters(params);
  if (!created) {
    report.errorCode = ErrorCode::VfsCreateFailed;
    report.win32Error = GetLastError();
    report.message = "创建虚拟文件系统失败";
    return false;
  }
  connected_ = true;

  usvfsClearExecutableBlacklist();
  for (const auto& exe : plan.blacklistExecutables) {
    usvfsBlacklistExecutable(exe.c_str());
  }
  usvfsClearSkipFileSuffixes();
  for (const auto& suffix : plan.skipFileSuffixes) {
    usvfsAddSkipFileSuffix(suffix.c_str());
  }
  usvfsClearSkipDirectories();
  for (const auto& dir : plan.skipDirectories) {
    usvfsAddSkipDirectory(dir.c_str());
  }
  usvfsClearLibraryForceLoads();
  for (const auto& load : plan.forceLoadLibraries) {
    usvfsForceLoadLibrary(load.process.c_str(), load.library.c_str());
  }
  return true;
}

bool VfsSession::ApplyMappings(const Plan& plan, Report& report)
{
  usvfsClearVirtualMappings();
  if (!LinkSkeleton(plan, report)) {
    usvfsClearVirtualMappings();
    return false;
  }

  // 与 MO2 相同：按清单顺序链接，后链接的覆盖先链接的。
  for (size_t i = 0; i < plan.mappings.size(); ++i) {
    const auto& m = plan.mappings[i];
    BOOL ok;
    if (m.type == MappingType::Directory) {
      const unsigned int flags =
          LINKFLAG_RECURSIVE | (m.createTarget ? LINKFLAG_CREATETARGET : 0u);
      ok = usvfsVirtualLinkDirectoryStatic(m.source.c_str(), m.destination.c_str(), flags);
    } else {
      ok = usvfsVirtualLinkFile(m.source.c_str(), m.destination.c_str(), 0);
    }
    if (ok) {
      ++report.mappingsApplied;
    } else {
      report.mappingFailures.push_back({i, GetLastError()});
    }
  }

  // 少一条映射，游戏看到的就是另一套 MOD 组合；宁可不启动也不带病启动。
  if (!report.mappingFailures.empty()) {
    report.errorCode = ErrorCode::MappingFailed;
    report.message = std::to_string(report.mappingFailures.size()) + " 条映射建立失败";
    usvfsClearVirtualMappings();
    return false;
  }
  return true;
}

bool VfsSession::LinkSkeleton(const Plan& plan, Report& report)
{
  const auto skeleton = PlanSkeleton(plan.mappings, [](const std::wstring& dir) {
    std::error_code ec;
    return fs::is_directory(fs::path(dir), ec);
  });
  if (skeleton.links.empty()) {
    return true;
  }

  const auto root = fs::temp_directory_path() / L"aivfs" /
                    Utf8ToWide(SharedMemoryName(plan.instanceName)) / L"skeleton";
  std::error_code ec;
  fs::remove_all(root, ec);
  skeletonRoot_ = root.wstring();

  for (size_t i = 0; i < skeleton.links.size(); ++i) {
    const auto& link = skeleton.links[i];
    const auto dir = root / std::to_wstring(i);
    for (const auto& rel : link.relativeDirectories) {
      fs::create_directories(rel.empty() ? dir : dir / rel, ec);
      if (ec) {
        report.errorCode = ErrorCode::MappingFailed;
        report.win32Error = static_cast<uint32_t>(ec.value());
        report.message = "无法创建骨架目录：" + WideToUtf8(dir.wstring());
        return false;
      }
    }
    // 骨架里只有空目录，不会遮住任何文件，所以放在所有映射之前不影响覆盖顺序。
    if (!usvfsVirtualLinkDirectoryStatic(dir.c_str(), link.missingRoot.c_str(),
                                         LINKFLAG_RECURSIVE)) {
      report.errorCode = ErrorCode::MappingFailed;
      report.win32Error = GetLastError();
      report.message = "无法建立虚拟目录：" + WideToUtf8(link.missingRoot);
      return false;
    }
  }
  return true;
}

bool VfsSession::Launch(const Plan& plan, Report& report, void*& processHandle)
{
  std::wstring commandLine = BuildCommandLine(plan.launch);
  const std::wstring workingDirectory =
      plan.launch.workingDirectory.empty()
          ? fs::path(plan.launch.executable).parent_path().wstring()
          : plan.launch.workingDirectory;

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION info{};
  // 挂起启动：先放进作业对象再恢复，保证它拉起的子进程一个不漏地归入作业。
  const BOOL ok = usvfsCreateProcessHooked(
      plan.launch.executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
      CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startup, &info);
  if (!ok) {
    const DWORD error = GetLastError();
    report.win32Error = error;
    if (error == ERROR_INVALID_PARAMETER) {
      // usvfs 在注入失败时杀掉进程并设置这个错误码。
      report.errorCode = ErrorCode::InjectionFailed;
      report.suspectedAntivirus = true;
      report.message = "无法把虚拟文件系统注入进程：" + WideToUtf8(plan.launch.executable);
    } else {
      report.errorCode = ErrorCode::ProcessCreateFailed;
      report.suspectedAntivirus = IsLikelyAntivirusError(error);
      report.message = "启动进程失败：" + WideToUtf8(plan.launch.executable);
    }
    return false;
  }

  job_ = CreateJobObjectW(nullptr, nullptr);
  if (job_ != nullptr && !AssignProcessToJobObject(job_, info.hProcess)) {
    CloseHandle(job_);
    job_ = nullptr;
  }
  ResumeThread(info.hThread);
  CloseHandle(info.hThread);

  processHandle = info.hProcess;
  report.rootProcessId = info.dwProcessId;
  report.processIds.push_back(info.dwProcessId);
  return true;
}

namespace
{
void RecordProcessId(Report& report, DWORD pid)
{
  if (std::find(report.processIds.begin(), report.processIds.end(), pid) ==
      report.processIds.end()) {
    report.processIds.push_back(pid);
  }
}
} // namespace

// 根进程往往只是加载器（skse64_loader 拉起游戏本体后立即退出），
// 虚拟文件系统要撑到整棵进程树都退出为止，否则游戏会在运行中丢失 MOD。
void VfsSession::WaitForProcessTree(void* rootProcess, Report& report)
{
  if (job_ == nullptr) {
    WaitWithUsvfsProcessList(rootProcess, report);
    return;
  }

  constexpr DWORD kMaxIds = 256;
  std::vector<unsigned char> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) +
                                    sizeof(ULONG_PTR) * kMaxIds);
  auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
  for (;;) {
    if (QueryInformationJobObject(job_, JobObjectBasicProcessIdList, list,
                                  static_cast<DWORD>(buffer.size()), nullptr) ||
        GetLastError() == ERROR_MORE_DATA) {
      for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
        RecordProcessId(report, static_cast<DWORD>(list->ProcessIdList[i]));
      }
    }
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if (!QueryInformationJobObject(job_, JobObjectBasicAccountingInformation, &accounting,
                                   sizeof(accounting), nullptr) ||
        accounting.ActiveProcesses == 0) {
      break;
    }
    Sleep(500);
  }

  WaitForSingleObject(rootProcess, INFINITE);
  DWORD exitCode = 0;
  if (GetExitCodeProcess(rootProcess, &exitCode)) {
    report.rootExitCode = exitCode;
  }
}

// 退路：进程无法放进作业对象时，改看 usvfs 自己登记的进程表。
// 子进程登记前有一个空窗，所以要连续几次都看不到才算结束。
void VfsSession::WaitWithUsvfsProcessList(void* rootProcess, Report& report)
{
  WaitForSingleObject(rootProcess, INFINITE);
  DWORD exitCode = 0;
  if (GetExitCodeProcess(rootProcess, &exitCode)) {
    report.rootExitCode = exitCode;
  }

  const DWORD self = GetCurrentProcessId();
  int emptyPolls = 0;
  for (;;) {
    size_t count = 0;
    DWORD* buffer = nullptr;
    if (!usvfsGetVFSProcessList2(&count, &buffer)) {
      break;
    }
    std::vector<DWORD> alive(buffer, buffer + count);
    std::free(buffer);
    alive.erase(std::remove(alive.begin(), alive.end(), self), alive.end());
    if (alive.empty()) {
      if (++emptyPolls >= 6) {
        break;
      }
    } else {
      emptyPolls = 0;
      for (DWORD pid : alive) {
        RecordProcessId(report, pid);
      }
    }
    Sleep(500);
  }
}

void VfsSession::StartLogPump(const std::wstring& logFile)
{
  std::error_code ec;
  fs::create_directories(fs::path(logFile).parent_path(), ec);
  logStream_.open(fs::path(logFile), std::ios::binary | std::ios::app);
  if (!logStream_) {
    return;
  }
  pumping_ = true;
  logThread_ = std::thread([this] {
    std::vector<char> buffer(4096);
    auto drain = [&] {
      while (usvfsGetLogMessages(buffer.data(), buffer.size(), false)) {
        logStream_ << buffer.data() << '\n';
      }
      logStream_.flush();
    };
    while (pumping_) {
      drain();
      Sleep(200);
    }
    drain();
  });
}

void VfsSession::StopLogPump()
{
  pumping_ = false;
  if (logThread_.joinable()) {
    logThread_.join();
  }
}
} // namespace aivfs
