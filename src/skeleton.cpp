#include "skeleton.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <map>
#include <set>

namespace aivfs
{
namespace
{
namespace fs = std::filesystem;

std::wstring Key(const fs::path& path)
{
  auto text = path.lexically_normal().wstring();
  std::transform(text.begin(), text.end(), text.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return text;
}
} // namespace

SkeletonPlan PlanSkeleton(const std::vector<Mapping>& mappings,
                          const std::function<bool(const std::wstring&)>& directoryExists)
{
  SkeletonPlan plan;
  // 按首次出现的顺序输出，结果可复现。
  std::vector<std::wstring> order;
  std::map<std::wstring, std::pair<fs::path, std::set<std::wstring>>> byRoot;
  std::map<std::wstring, bool> existsCache;

  auto exists = [&](const fs::path& dir) {
    const auto key = Key(dir);
    if (auto it = existsCache.find(key); it != existsCache.end()) {
      return it->second;
    }
    const bool result = directoryExists(dir.wstring());
    existsCache.emplace(key, result);
    return result;
  };

  for (size_t i = 0; i < mappings.size(); ++i) {
    const auto& m = mappings[i];
    if (m.type != MappingType::File) {
      continue;
    }
    const fs::path parent = fs::path(m.destination).parent_path();
    if (exists(parent)) {
      continue;
    }

    // 往上找到最上层缺失的目录：它的父目录存在。
    fs::path missingRoot = parent;
    bool reachable = false;
    for (;;) {
      const fs::path up = missingRoot.parent_path();
      if (up.empty() || up == missingRoot) {
        break;
      }
      if (exists(up)) {
        reachable = true;
        break;
      }
      missingRoot = up;
    }
    if (!reachable) {
      plan.unreachableMappings.push_back(i);
      continue;
    }

    const auto rootKey = Key(missingRoot);
    auto [it, inserted] = byRoot.try_emplace(rootKey, missingRoot, std::set<std::wstring>{});
    if (inserted) {
      order.push_back(rootKey);
    }
    const auto relative = parent.lexically_relative(missingRoot).lexically_normal();
    it->second.second.insert(relative == L"." ? std::wstring() : relative.wstring());
  }

  for (const auto& key : order) {
    auto& [root, relatives] = byRoot[key];
    SkeletonLink link;
    link.missingRoot = root.wstring();
    // 只保留叶子目录：建叶子时中间目录会一并建出。
    for (const auto& rel : relatives) {
      const bool hasDeeper = std::any_of(relatives.begin(), relatives.end(), [&](const auto& other) {
        return other.size() > rel.size() &&
               (rel.empty() || (Key(other).rfind(Key(rel) + L"\\", 0) == 0));
      });
      if (!hasDeeper) {
        link.relativeDirectories.push_back(rel);
      }
    }
    plan.links.push_back(std::move(link));
  }
  return plan;
}
} // namespace aivfs
