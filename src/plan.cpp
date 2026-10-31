#include "plan.hpp"

#include "text.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <stdexcept>

namespace aivfs
{
namespace
{
using nlohmann::json;
namespace fs = std::filesystem;

class Reader
{
public:
  explicit Reader(std::vector<PlanError>& errors) : errors_(errors) {}

  void Add(std::string field, std::string message)
  {
    errors_.push_back({std::move(field), std::move(message)});
  }

  // 未知字段一律报错：清单由主程序生成，多出来的字段只可能是拼错或版本不匹配。
  void RejectUnknownKeys(const json& object, const std::string& field,
                         std::initializer_list<const char*> allowed)
  {
    for (const auto& item : object.items()) {
      const bool known = std::any_of(allowed.begin(), allowed.end(),
                                     [&](const char* key) { return item.key() == key; });
      if (!known) {
        Add(Join(field, item.key()), "未知字段");
      }
    }
  }

  std::optional<std::string> String(const json& object, const char* key,
                                    const std::string& field, bool required)
  {
    const auto it = object.find(key);
    const auto path = Join(field, key);
    if (it == object.end()) {
      if (required) {
        Add(path, "缺少必填字段");
      }
      return std::nullopt;
    }
    if (!it->is_string()) {
      Add(path, "应为字符串");
      return std::nullopt;
    }
    return it->get<std::string>();
  }

  std::optional<std::wstring> WideString(const json& object, const char* key,
                                         const std::string& field, bool required)
  {
    auto value = String(object, key, field, required);
    if (!value) {
      return std::nullopt;
    }
    try {
      return Utf8ToWide(*value);
    } catch (const std::runtime_error& e) {
      Add(Join(field, key), e.what());
      return std::nullopt;
    }
  }

  std::optional<std::wstring> AbsolutePath(const json& object, const char* key,
                                           const std::string& field, bool required)
  {
    auto value = WideString(object, key, field, required);
    if (!value) {
      return std::nullopt;
    }
    const auto path = Join(field, key);
    if (value->empty()) {
      Add(path, "不能为空");
      return std::nullopt;
    }
    const fs::path p(*value);
    if (!p.is_absolute()) {
      Add(path, "必须是绝对路径");
      return std::nullopt;
    }
    for (const auto& part : p) {
      if (part == L"..") {
        Add(path, "不能包含 .. 段");
        return std::nullopt;
      }
    }
    return value;
  }

  std::vector<std::wstring> StringArray(const json& object, const char* key,
                                        const std::string& field)
  {
    std::vector<std::wstring> result;
    const auto it = object.find(key);
    if (it == object.end()) {
      return result;
    }
    const auto path = Join(field, key);
    if (!it->is_array()) {
      Add(path, "应为字符串数组");
      return result;
    }
    for (size_t i = 0; i < it->size(); ++i) {
      const auto& item = (*it)[i];
      const auto itemPath = path + "[" + std::to_string(i) + "]";
      if (!item.is_string() || item.get<std::string>().empty()) {
        Add(itemPath, "应为非空字符串");
        continue;
      }
      try {
        result.push_back(Utf8ToWide(item.get<std::string>()));
      } catch (const std::runtime_error& e) {
        Add(itemPath, e.what());
      }
    }
    return result;
  }

  static std::string Join(const std::string& field, const std::string& key)
  {
    return field == "$" ? key : field + "." + key;
  }

private:
  std::vector<PlanError>& errors_;
};

bool IsValidInstanceName(const std::string& name)
{
  // 会成为共享内存对象名的一部分。
  if (name.empty() || name.size() > 48) {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
  });
}

std::optional<LogLevel> ParseLogLevel(const std::string& text)
{
  if (text == "debug") return LogLevel::Debug;
  if (text == "info") return LogLevel::Info;
  if (text == "warning") return LogLevel::Warning;
  if (text == "error") return LogLevel::Error;
  return std::nullopt;
}

void ParseMappings(const json& root, Reader& reader, Plan& plan)
{
  const auto it = root.find("mappings");
  if (it == root.end()) {
    reader.Add("mappings", "缺少必填字段");
    return;
  }
  if (!it->is_array()) {
    reader.Add("mappings", "应为数组");
    return;
  }
  if (it->empty()) {
    reader.Add("mappings", "至少需要一条映射");
    return;
  }
  for (size_t i = 0; i < it->size(); ++i) {
    const auto& item = (*it)[i];
    const auto field = "mappings[" + std::to_string(i) + "]";
    if (!item.is_object()) {
      reader.Add(field, "应为对象");
      continue;
    }
    reader.RejectUnknownKeys(item, field, {"type", "source", "destination", "createTarget"});

    Mapping mapping;
    bool ok = true;
    const auto type = reader.String(item, "type", field, true);
    if (type == "directory") {
      mapping.type = MappingType::Directory;
    } else if (type == "file") {
      mapping.type = MappingType::File;
    } else {
      if (type) {
        reader.Add(field + ".type", "只能是 directory 或 file");
      }
      ok = false;
    }

    auto source = reader.AbsolutePath(item, "source", field, true);
    auto destination = reader.AbsolutePath(item, "destination", field, true);
    ok = ok && source && destination;

    if (const auto ct = item.find("createTarget"); ct != item.end()) {
      if (!ct->is_boolean()) {
        reader.Add(field + ".createTarget", "应为布尔值");
        ok = false;
      } else {
        mapping.createTarget = ct->get<bool>();
        if (mapping.createTarget && mapping.type == MappingType::File) {
          reader.Add(field + ".createTarget", "只能用于目录映射");
          ok = false;
        }
      }
    }

    if (ok) {
      mapping.source = std::move(*source);
      mapping.destination = std::move(*destination);
      plan.mappings.push_back(std::move(mapping));
    }
  }

  // 新建文件只能落到一个地方，多个 createTarget 指向同一目的地时 usvfs 的行为不确定。
  std::set<std::wstring> createTargets;
  for (const auto& m : plan.mappings) {
    if (!m.createTarget) continue;
    auto key = fs::path(m.destination).lexically_normal().wstring();
    while (key.size() > 3 && (key.back() == L'\\' || key.back() == L'/')) {
      key.pop_back();
    }
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (!createTargets.insert(key).second) {
      reader.Add("mappings", "同一个目的地只能有一条 createTarget 映射");
    }
  }
}

void ParseForceLoads(const json& root, Reader& reader, Plan& plan)
{
  const auto it = root.find("forceLoadLibraries");
  if (it == root.end()) {
    return;
  }
  if (!it->is_array()) {
    reader.Add("forceLoadLibraries", "应为数组");
    return;
  }
  for (size_t i = 0; i < it->size(); ++i) {
    const auto& item = (*it)[i];
    const auto field = "forceLoadLibraries[" + std::to_string(i) + "]";
    if (!item.is_object()) {
      reader.Add(field, "应为对象");
      continue;
    }
    reader.RejectUnknownKeys(item, field, {"process", "library"});
    auto process = reader.WideString(item, "process", field, true);
    auto library = reader.AbsolutePath(item, "library", field, true);
    if (process && process->empty()) {
      reader.Add(field + ".process", "不能为空");
      continue;
    }
    if (process && library) {
      plan.forceLoadLibraries.push_back({std::move(*process), std::move(*library)});
    }
  }
}

void ParseLaunch(const json& root, Reader& reader, Plan& plan)
{
  const auto it = root.find("launch");
  if (it == root.end()) {
    reader.Add("launch", "缺少必填字段");
    return;
  }
  if (!it->is_object()) {
    reader.Add("launch", "应为对象");
    return;
  }
  reader.RejectUnknownKeys(*it, "launch", {"executable", "arguments", "workingDirectory"});
  if (auto exe = reader.AbsolutePath(*it, "executable", "launch", true)) {
    plan.launch.executable = std::move(*exe);
  }
  if (auto args = reader.WideString(*it, "arguments", "launch", false)) {
    plan.launch.arguments = std::move(*args);
  }
  if (auto cwd = reader.AbsolutePath(*it, "workingDirectory", "launch", false)) {
    plan.launch.workingDirectory = std::move(*cwd);
  }
}
} // namespace

ParseResult ParsePlan(std::string_view text)
{
  ParseResult result;
  Reader reader(result.errors);

  const json root = json::parse(text.begin(), text.end(), nullptr, false);
  if (root.is_discarded()) {
    reader.Add("$", "不是合法的 JSON");
    return result;
  }
  if (!root.is_object()) {
    reader.Add("$", "顶层应为对象");
    return result;
  }

  reader.RejectUnknownKeys(root, "$",
                           {"schemaVersion", "instanceName", "logLevel", "logFile",
                            "crashDumpPath", "mappings", "skipFileSuffixes", "skipDirectories",
                            "blacklistExecutables", "forceLoadLibraries", "launch"});

  Plan plan;

  // 版本不对时后面的字段含义可能都变了，直接停下，避免一堆误导性的报错。
  const auto version = root.find("schemaVersion");
  if (version == root.end() || !version->is_number_integer()) {
    reader.Add("schemaVersion", "缺少或不是整数");
    return result;
  }
  plan.schemaVersion = version->get<int>();
  if (plan.schemaVersion != kPlanSchemaVersion) {
    reader.Add("schemaVersion", "不支持的版本 " + std::to_string(plan.schemaVersion) +
                                    "，本启动器只认 " + std::to_string(kPlanSchemaVersion));
    return result;
  }

  if (auto name = reader.String(root, "instanceName", "$", true)) {
    if (IsValidInstanceName(*name)) {
      plan.instanceName = *name;
    } else {
      reader.Add("instanceName", "只能用 1-48 个英文字母、数字、下划线或连字符");
    }
  }

  if (auto level = reader.String(root, "logLevel", "$", false)) {
    if (auto parsed = ParseLogLevel(*level)) {
      plan.logLevel = *parsed;
    } else {
      reader.Add("logLevel", "只能是 debug、info、warning、error");
    }
  }

  if (auto logFile = reader.AbsolutePath(root, "logFile", "$", false)) {
    plan.logFile = std::move(*logFile);
  }
  if (auto dumps = reader.AbsolutePath(root, "crashDumpPath", "$", false)) {
    plan.crashDumpPath = std::move(*dumps);
  }

  ParseMappings(root, reader, plan);
  plan.skipFileSuffixes = reader.StringArray(root, "skipFileSuffixes", "$");
  plan.skipDirectories = reader.StringArray(root, "skipDirectories", "$");
  plan.blacklistExecutables = reader.StringArray(root, "blacklistExecutables", "$");
  ParseForceLoads(root, reader, plan);
  ParseLaunch(root, reader, plan);

  if (result.errors.empty()) {
    result.plan = std::move(plan);
  }
  return result;
}

std::vector<PlanError> CheckPlanPaths(const Plan& plan)
{
  std::vector<PlanError> errors;
  std::error_code ec;

  for (size_t i = 0; i < plan.mappings.size(); ++i) {
    const auto& m = plan.mappings[i];
    const auto field = "mappings[" + std::to_string(i) + "].source";
    const fs::path source(m.source);
    if (m.type == MappingType::Directory) {
      if (!fs::is_directory(source, ec)) {
        errors.push_back({field, "目录不存在"});
      }
    } else if (!fs::is_regular_file(source, ec)) {
      errors.push_back({field, "文件不存在"});
    }
  }

  if (!fs::is_regular_file(fs::path(plan.launch.executable), ec)) {
    errors.push_back({"launch.executable", "文件不存在"});
  }
  if (!plan.launch.workingDirectory.empty() &&
      !fs::is_directory(fs::path(plan.launch.workingDirectory), ec)) {
    errors.push_back({"launch.workingDirectory", "目录不存在"});
  }
  for (size_t i = 0; i < plan.forceLoadLibraries.size(); ++i) {
    if (!fs::is_regular_file(fs::path(plan.forceLoadLibraries[i].library), ec)) {
      errors.push_back({"forceLoadLibraries[" + std::to_string(i) + "].library", "文件不存在"});
    }
  }
  return errors;
}

std::wstring BuildCommandLine(const LaunchSpec& launch)
{
  std::wstring commandLine = L"\"" + launch.executable + L"\"";
  if (!launch.arguments.empty()) {
    commandLine += L" ";
    commandLine += launch.arguments;
  }
  return commandLine;
}
} // namespace aivfs
