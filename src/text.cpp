#include "text.hpp"

#include <windows.h>

#include <limits>
#include <stdexcept>

namespace aivfs
{
std::wstring Utf8ToWide(std::string_view text)
{
  if (text.empty()) {
    return {};
  }
  if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("文本过长");
  }
  const int size = static_cast<int>(text.size());
  const int needed =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
  if (needed <= 0) {
    throw std::runtime_error("不是合法的 UTF-8 文本");
  }
  std::wstring result(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, result.data(), needed);
  return result;
}

std::string WideToUtf8(std::wstring_view text)
{
  if (text.empty()) {
    return {};
  }
  if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("文本过长");
  }
  const int size = static_cast<int>(text.size());
  const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
                                         nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    throw std::runtime_error("包含无法编码为 UTF-8 的字符");
  }
  std::string result(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, result.data(), needed,
                      nullptr, nullptr);
  return result;
}
} // namespace aivfs
