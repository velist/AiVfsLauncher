#include "skeleton.hpp"

#include <doctest.h>

#include <algorithm>
#include <cwctype>
#include <set>
#include <string>

using namespace aivfs;

namespace
{
// 假文件系统：给出真实存在的目录集合（大小写不敏感）。
struct FakeDisk
{
  std::set<std::wstring> dirs;

  static std::wstring Lower(std::wstring s)
  {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
  }

  bool operator()(const std::wstring& dir) const { return dirs.count(Lower(dir)) > 0; }
};

Mapping File(std::wstring destination)
{
  return {MappingType::File, L"D:\\Repo\\src.bin", std::move(destination), false};
}

FakeDisk GameDisk()
{
  return {{L"c:\\", L"c:\\game", L"c:\\game\\data", L"c:\\game\\data\\meshes"}};
}
} // namespace

TEST_CASE("父目录都存在时不需要骨架")
{
  const auto plan =
      PlanSkeleton({File(L"C:\\Game\\Data\\a.esp"), File(L"C:\\Game\\Data\\Meshes\\b.nif")},
                   GameDisk());
  CHECK(plan.links.empty());
  CHECK(plan.unreachableMappings.empty());
}

TEST_CASE("缺失的目录链只在最上层缺失处链接一次，只建叶子目录")
{
  const auto plan = PlanSkeleton(
      {
          File(L"C:\\Game\\Data\\textures\\爱酱\\armor\\chest.dds"),
          File(L"C:\\Game\\Data\\textures\\爱酱\\armor\\legs.dds"),
          File(L"C:\\Game\\Data\\textures\\爱酱\\hair.dds"),
          File(L"C:\\Game\\Data\\Textures\\other\\x.dds"),
      },
      GameDisk());
  REQUIRE(plan.links.size() == 1);
  CHECK(plan.links[0].missingRoot == L"C:\\Game\\Data\\textures");
  const auto& rel = plan.links[0].relativeDirectories;
  CHECK(rel.size() == 2);
  CHECK(std::find(rel.begin(), rel.end(), L"爱酱\\armor") != rel.end());
  CHECK(std::find(rel.begin(), rel.end(), L"other") != rel.end());
}

TEST_CASE("文件直接放在缺失目录里：相对目录为空")
{
  const auto plan = PlanSkeleton({File(L"C:\\Game\\Data\\SKSE\\Plugins.txt")}, GameDisk());
  REQUIRE(plan.links.size() == 1);
  CHECK(plan.links[0].missingRoot == L"C:\\Game\\Data\\SKSE");
  CHECK(plan.links[0].relativeDirectories == std::vector<std::wstring>{L""});
}

TEST_CASE("不同的缺失根按首次出现顺序输出")
{
  const auto plan = PlanSkeleton(
      {
          File(L"C:\\Game\\Data\\scripts\\a.pex"),
          File(L"C:\\Game\\Data\\meshes\\new\\b.nif"),
          File(L"C:\\Game\\Data\\scripts\\source\\a.psc"),
      },
      GameDisk());
  REQUIRE(plan.links.size() == 2);
  CHECK(plan.links[0].missingRoot == L"C:\\Game\\Data\\scripts");
  CHECK(plan.links[0].relativeDirectories == std::vector<std::wstring>{L"source"});
  CHECK(plan.links[1].missingRoot == L"C:\\Game\\Data\\meshes\\new");
}

TEST_CASE("目录映射不需要骨架；一路到盘符都不存在的记为不可达")
{
  Mapping dir{MappingType::Directory, L"D:\\Repo\\x", L"C:\\Game\\Data\\new", false};
  const auto plan = PlanSkeleton({dir, File(L"Q:\\nowhere\\a.txt")}, GameDisk());
  CHECK(plan.links.empty());
  CHECK(plan.unreachableMappings == std::vector<size_t>{1});
}
