#pragma once

#include "plan.hpp"

#include <functional>
#include <string>
#include <vector>

namespace aivfs
{
// usvfs 链接单个文件时，要求目标的父目录在磁盘上或虚拟树里已经存在
// （usvfs.cpp 的 assertPathExists），否则返回 ERROR_PATH_NOT_FOUND。
// MO2 整目录链接所以碰不到；我们按最终视图逐文件映射，目标目录常常是 MOD 新增的。
//
// 解决办法：对每条缺失的目录链，取最上层缺失的那一级 A（A 的父目录真实存在），
// 在启动器自己的骨架目录里建出同样形状的空目录，再把骨架目录链接到 A。
// 之后 usvfs 沿着链接目标就能找到每一级中间目录。
struct SkeletonLink
{
  // 游戏里最上层缺失的目录，如 C:\Game\Data\textures。
  std::wstring missingRoot;
  // 骨架目录里需要建出的子目录（相对 missingRoot），如 L"爱酱\\armor"；空串表示只要 missingRoot 本身。
  std::vector<std::wstring> relativeDirectories;
};

struct SkeletonPlan
{
  std::vector<SkeletonLink> links;
  // 一路找到盘符根都不存在的文件映射（例如盘符不存在），这些映射无法建立。
  std::vector<size_t> unreachableMappings;
};

// 纯函数：directoryExists 由调用方注入，单测里可以用假的文件系统。
SkeletonPlan PlanSkeleton(const std::vector<Mapping>& mappings,
                          const std::function<bool(const std::wstring&)>& directoryExists);
} // namespace aivfs
