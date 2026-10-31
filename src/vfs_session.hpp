#pragma once

#include "plan.hpp"
#include "report.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <fstream>
#include <string>
#include <thread>

namespace aivfs
{
// usvfs 的薄封装：建虚拟文件系统 → 按清单链接 → 挂钩启动进程 → 等所有被挂钩进程退出。
// 一次启动用一个实例，析构时断开；结果全部写进 Report，不抛异常。
class VfsSession
{
public:
  VfsSession() = default;
  VfsSession(const VfsSession&) = delete;
  VfsSession& operator=(const VfsSession&) = delete;
  ~VfsSession();

  // 必须在调用任何 usvfs 函数之前执行：usvfs DLL 是延迟加载的，
  // 这里显式从启动器所在目录加载，失败就能如实报告，而不是在第一次调用时崩溃。
  static bool LoadUsvfs(Report& report);

  // onStarted 在游戏进程拉起后、开始等待之前调用一次。
  bool Run(const Plan& plan, Report& report, const std::function<void(const Report&)>& onStarted);

private:
  bool Create(const Plan& plan, Report& report);
  bool ApplyMappings(const Plan& plan, Report& report);
  bool LinkSkeleton(const Plan& plan, Report& report);
  bool Launch(const Plan& plan, Report& report, void*& processHandle);
  void WaitForProcessTree(void* rootProcess, Report& report);
  void WaitWithUsvfsProcessList(void* rootProcess, Report& report);
  void StartLogPump(const std::wstring& logFile);
  void StopLogPump();

  bool connected_ = false;
  // 作业对象：子进程自动归入，用它判断"整棵进程树都退出了"。
  void* job_ = nullptr;
  // 启动器自己的骨架目录（见 skeleton.hpp），结束时删除。
  std::wstring skeletonRoot_;
  std::atomic<bool> pumping_{false};
  std::thread logThread_;
  std::ofstream logStream_;
};
} // namespace aivfs
