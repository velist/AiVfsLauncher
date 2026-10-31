#include "report.hpp"

#include <windows.h>

#include <doctest.h>
#include <nlohmann/json.hpp>

using namespace aivfs;

TEST_CASE("游戏运行中：status=running")
{
  Report report;
  report.rootProcessId = 42;
  const auto j = nlohmann::json::parse(SerializeReport(report));
  CHECK(j["status"] == "running");
  CHECK(j["rootProcessId"] == 42);
}

TEST_CASE("成功报告：status=ok，可空字段输出 null")
{
  Report report;
  report.finished = true;
  const auto j = nlohmann::json::parse(SerializeReport(report));
  CHECK(j["status"] == "ok");
  CHECK(j["errorCode"] == "none");
  CHECK(j["rootProcessId"].is_null());
  CHECK(j["rootExitCode"].is_null());
  CHECK(j["planErrors"].empty());
}

TEST_CASE("失败报告带上错误详情")
{
  Report report;
  report.finished = true;
  report.errorCode = ErrorCode::MappingFailed;
  report.message = "1 条映射建立失败";
  report.mappingFailures.push_back({3, 5});
  report.planErrors.push_back({"mappings[0].source", "目录不存在"});
  report.rootExitCode = 0xC0000005u;

  const auto j = nlohmann::json::parse(SerializeReport(report));
  CHECK(j["status"] == "error");
  CHECK(j["errorCode"] == "mappingFailed");
  CHECK(j["message"] == "1 条映射建立失败");
  CHECK(j["mappingFailures"][0]["index"] == 3);
  CHECK(j["mappingFailures"][0]["win32Error"] == 5);
  CHECK(j["planErrors"][0]["message"] == "目录不存在");
  CHECK(j["rootExitCode"] == 0xC0000005u);
}

TEST_CASE("退出码取值固定，主程序依赖它")
{
  CHECK(static_cast<int>(ErrorCode::None) == 0);
  CHECK(static_cast<int>(ErrorCode::InvalidPlan) == 4);
  CHECK(static_cast<int>(ErrorCode::UsvfsUnavailable) == 6);
  CHECK(static_cast<int>(ErrorCode::ProcessCreateFailed) == 9);
  CHECK(static_cast<int>(ErrorCode::InjectionFailed) == 11);
}

TEST_CASE("杀软特征错误码")
{
  CHECK(IsLikelyAntivirusError(ERROR_ACCESS_DENIED));
  CHECK(IsLikelyAntivirusError(ERROR_VIRUS_INFECTED));
  CHECK_FALSE(IsLikelyAntivirusError(ERROR_FILE_NOT_FOUND));
}
