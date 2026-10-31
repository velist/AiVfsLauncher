#include "plan.hpp"

#include <doctest.h>

#include <algorithm>
#include <string>

using namespace aivfs;

namespace
{
const char* kValidPlan = R"({
  "schemaVersion": 1,
  "instanceName": "skyrimse_default",
  "logLevel": "info",
  "mappings": [
    { "type": "directory", "source": "D:\\Repo\\SkyUI", "destination": "C:\\Games\\Skyrim\\Data" },
    { "type": "directory", "source": "D:\\Repo\\爱酱 汉化", "destination": "C:\\Games\\Skyrim\\Data" },
    { "type": "file", "source": "D:\\Profiles\\默认\\plugins.txt",
      "destination": "C:\\Users\\a\\AppData\\Local\\Skyrim Special Edition\\plugins.txt" },
    { "type": "directory", "source": "D:\\Overwrite", "destination": "C:\\Games\\Skyrim\\Data",
      "createTarget": true }
  ],
  "skipFileSuffixes": [".mohidden"],
  "forceLoadLibraries": [ { "process": "SkyrimSE.exe", "library": "C:\\x\\a.dll" } ],
  "launch": { "executable": "C:\\Games\\Skyrim\\skse64_loader.exe", "arguments": "-forcesteamloader" }
})";

bool HasError(const ParseResult& result, const std::string& field)
{
  return std::any_of(result.errors.begin(), result.errors.end(),
                     [&](const PlanError& e) { return e.field == field; });
}

std::string Replace(std::string text, const std::string& from, const std::string& to)
{
  const auto pos = text.find(from);
  REQUIRE(pos != std::string::npos);
  text.replace(pos, from.size(), to);
  return text;
}
} // namespace

TEST_CASE("合法清单完整解析，映射顺序保持不变")
{
  const auto result = ParsePlan(kValidPlan);
  REQUIRE(result.errors.empty());
  REQUIRE(result.plan);
  const auto& plan = *result.plan;

  CHECK(plan.instanceName == "skyrimse_default");
  CHECK(plan.logLevel == LogLevel::Info);
  REQUIRE(plan.mappings.size() == 4);
  CHECK(plan.mappings[0].source == L"D:\\Repo\\SkyUI");
  CHECK(plan.mappings[1].source == L"D:\\Repo\\爱酱 汉化");
  CHECK(plan.mappings[2].type == MappingType::File);
  CHECK(plan.mappings[3].createTarget);
  CHECK_FALSE(plan.mappings[0].createTarget);
  CHECK(plan.skipFileSuffixes == std::vector<std::wstring>{L".mohidden"});
  REQUIRE(plan.forceLoadLibraries.size() == 1);
  CHECK(plan.forceLoadLibraries[0].process == L"SkyrimSE.exe");
  CHECK(plan.launch.executable == L"C:\\Games\\Skyrim\\skse64_loader.exe");
  CHECK(plan.launch.arguments == L"-forcesteamloader");
  CHECK(plan.launch.workingDirectory.empty());
}

TEST_CASE("日志级别缺省为 warning")
{
  const auto result = ParsePlan(Replace(kValidPlan, R"("logLevel": "info",)", ""));
  REQUIRE(result.plan);
  CHECK(result.plan->logLevel == LogLevel::Warning);
}

TEST_CASE("不是 JSON 或顶层不是对象")
{
  CHECK(HasError(ParsePlan("{ not json"), "$"));
  CHECK(HasError(ParsePlan("[]"), "$"));
}

TEST_CASE("版本不对时立即停止，只报版本一条错")
{
  const auto result = ParsePlan(Replace(kValidPlan, R"("schemaVersion": 1)", R"("schemaVersion": 2)"));
  CHECK_FALSE(result.plan);
  REQUIRE(result.errors.size() == 1);
  CHECK(result.errors[0].field == "schemaVersion");
}

TEST_CASE("未知字段报错，防止拼写错误被静默忽略")
{
  const auto top = ParsePlan(Replace(kValidPlan, R"("logLevel")", R"("logLevl")"));
  CHECK(HasError(top, "logLevl"));

  const auto nested = ParsePlan(Replace(kValidPlan, R"("createTarget": true)", R"("createTaget": true)"));
  CHECK(HasError(nested, "mappings[3].createTaget"));
}

TEST_CASE("实例名只允许安全字符")
{
  CHECK(HasError(ParsePlan(Replace(kValidPlan, "skyrimse_default", "老滚")), "instanceName"));
  CHECK(HasError(ParsePlan(Replace(kValidPlan, "skyrimse_default", "a/b")), "instanceName"));
  CHECK(HasError(ParsePlan(Replace(kValidPlan, "skyrimse_default", std::string(49, 'a'))),
                 "instanceName"));
  CHECK(ParsePlan(Replace(kValidPlan, "skyrimse_default", std::string(48, 'a'))).plan);
}

TEST_CASE("路径必须是绝对路径且不含 ..")
{
  CHECK(HasError(ParsePlan(Replace(kValidPlan, R"("D:\\Repo\\SkyUI")", R"("Repo\\SkyUI")")),
                 "mappings[0].source"));
  CHECK(HasError(ParsePlan(Replace(kValidPlan, R"("D:\\Repo\\SkyUI")", R"("D:Repo")")),
                 "mappings[0].source"));
  CHECK(HasError(ParsePlan(Replace(kValidPlan, R"("D:\\Repo\\SkyUI")", R"("D:\\Repo\\..\\Windows")")),
                 "mappings[0].source"));
  CHECK(HasError(ParsePlan(Replace(kValidPlan, R"("D:\\Repo\\SkyUI")", R"("")")),
                 "mappings[0].source"));
  CHECK(ParsePlan(Replace(kValidPlan, R"("D:\\Repo\\SkyUI")", R"("\\\\nas\\mods\\SkyUI")")).plan);
}

TEST_CASE("映射类型与 createTarget 的组合")
{
  CHECK(HasError(ParsePlan(Replace(kValidPlan, R"("type": "file")", R"("type": "link")")),
                 "mappings[2].type"));
  const auto fileWithCreate = ParsePlan(Replace(
      kValidPlan, R"("type": "file", "source": "D:\\Profiles\\默认\\plugins.txt",)",
      R"("type": "file", "createTarget": true, "source": "D:\\Profiles\\默认\\plugins.txt",)"));
  CHECK(HasError(fileWithCreate, "mappings[2].createTarget"));
}

TEST_CASE("同一目的地不能有两条 createTarget")
{
  const auto result = ParsePlan(Replace(
      kValidPlan,
      R"({ "type": "directory", "source": "D:\\Repo\\SkyUI", "destination": "C:\\Games\\Skyrim\\Data" })",
      R"({ "type": "directory", "source": "D:\\Repo\\SkyUI", "destination": "c:\\games\\skyrim\\data\\", "createTarget": true })"));
  CHECK(HasError(result, "mappings"));
}

TEST_CASE("映射列表不能为空，launch 必填")
{
  const char* empty = R"({ "schemaVersion": 1, "instanceName": "x", "mappings": [],
                           "launch": { "executable": "C:\\a.exe" } })";
  CHECK(HasError(ParsePlan(empty), "mappings"));

  const char* noLaunch = R"({ "schemaVersion": 1, "instanceName": "x",
      "mappings": [ { "type": "directory", "source": "C:\\a", "destination": "C:\\b" } ] })";
  CHECK(HasError(ParsePlan(noLaunch), "launch"));
}

TEST_CASE("一次报出所有错误，而不是遇到第一个就停")
{
  const char* bad = R"({ "schemaVersion": 1, "instanceName": "",
      "mappings": [ { "type": "dir", "source": "a", "destination": "b" } ],
      "launch": { "executable": "game.exe" } })";
  const auto result = ParsePlan(bad);
  CHECK(HasError(result, "instanceName"));
  CHECK(HasError(result, "mappings[0].type"));
  CHECK(HasError(result, "mappings[0].source"));
  CHECK(HasError(result, "mappings[0].destination"));
  CHECK(HasError(result, "launch.executable"));
}

TEST_CASE("命令行：可执行路径加引号，参数原样拼接")
{
  CHECK(BuildCommandLine({L"C:\\Program Files\\Game\\game.exe", L"", L""}) ==
        L"\"C:\\Program Files\\Game\\game.exe\"");
  CHECK(BuildCommandLine({L"C:\\g.exe", L"-a \"b c\"", L""}) == L"\"C:\\g.exe\" -a \"b c\"");
}
