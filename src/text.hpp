#pragma once

#include <string>
#include <string_view>

namespace aivfs
{
// 清单与报告一律是 UTF-8 JSON，Win32 API 一律用宽字符；两者只在这里互转。
// 非法编码抛 std::runtime_error，不做"尽量转"——路径错一个字就会映射到别处。
std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);
} // namespace aivfs
