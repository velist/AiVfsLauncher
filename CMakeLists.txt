cmake_minimum_required(VERSION 3.24)

project(AiVfsLauncher VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "AiVfsLauncher 只支持 x64 构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# 依赖一律固定版本 + SHA256，避免构建时拿到被替换的二进制。
# usvfs：MO2 官方发布的预编译包（GPL-3.0），含头文件、导入库、DLL 与 proxy。
FetchContent_Declare(usvfs_dist
  URL https://github.com/ModOrganizer2/usvfs/releases/download/v0.5.7.2/usvfs_v0.5.7.2.7z
  URL_HASH SHA256=c6252eed78ee1c307733a4412cb68522cffc48107be4795c4e38b2b8d7c76d01
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(doctest_header
  URL https://raw.githubusercontent.com/doctest/doctest/v2.4.12/doctest/doctest.h
  URL_HASH SHA256=94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced
  DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(usvfs_dist nlohmann_json doctest_header)

set(USVFS_DIR ${usvfs_dist_SOURCE_DIR})
set(USVFS_RUNTIME_FILES
  ${USVFS_DIR}/bin/usvfs_x64.dll
  ${USVFS_DIR}/bin/usvfs_x86.dll
  ${USVFS_DIR}/bin/usvfs_proxy_x64.exe
  ${USVFS_DIR}/bin/usvfs_proxy_x86.exe)

add_library(usvfs SHARED IMPORTED)
set_target_properties(usvfs PROPERTIES
  IMPORTED_LOCATION ${USVFS_DIR}/bin/usvfs_x64.dll
  IMPORTED_IMPLIB ${USVFS_DIR}/lib/usvfs_x64.lib
  INTERFACE_INCLUDE_DIRECTORIES ${USVFS_DIR}/include)

function(aivfs_set_options target)
  target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive- /EHsc)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endfunction()

# 纯逻辑：清单解析、报告序列化、编码转换。不依赖 usvfs，单测只链它。
add_library(aivfs_core STATIC
  src/plan.cpp
  src/report.cpp
  src/skeleton.cpp
  src/text.cpp)
target_include_directories(aivfs_core PUBLIC src)
target_link_libraries(aivfs_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(aivfs_core PUBLIC AIVFS_VERSION="${PROJECT_VERSION}")
aivfs_set_options(aivfs_core)

add_executable(AiVfsLauncher
  src/main.cpp
  src/vfs_session.cpp
  src/launcher.rc)
target_link_libraries(AiVfsLauncher PRIVATE aivfs_core usvfs delayimp)
# 延迟加载：usvfs DLL 被杀软隔离时，进程仍能启动并写出"usvfs 不可用"的报告，
# 而不是在加载阶段就以 0xC0000135 退出、让主程序无从判断原因。
target_link_options(AiVfsLauncher PRIVATE /DELAYLOAD:usvfs_x64.dll)
aivfs_set_options(AiVfsLauncher)
add_custom_command(TARGET AiVfsLauncher POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${USVFS_RUNTIME_FILES} $<TARGET_FILE_DIR:AiVfsLauncher>)

# 测试用的"假游戏"：在虚拟文件系统里列目录、读写文件，把看到的结果写成 JSON。
add_executable(aivfs_probe probe/vfs_probe.cpp)
target_link_libraries(aivfs_probe PRIVATE aivfs_core)
aivfs_set_options(aivfs_probe)

include(CTest)
if(BUILD_TESTING)
  add_executable(aivfs_tests tests/plan_tests.cpp tests/report_tests.cpp tests/skeleton_tests.cpp tests/test_main.cpp)
  target_include_directories(aivfs_tests PRIVATE ${doctest_header_SOURCE_DIR})
  target_link_libraries(aivfs_tests PRIVATE aivfs_core)
  aivfs_set_options(aivfs_tests)
  add_test(NAME unit COMMAND aivfs_tests)

  add_executable(aivfs_e2e tests/e2e_test.cpp)
  target_link_libraries(aivfs_e2e PRIVATE aivfs_core)
  aivfs_set_options(aivfs_e2e)
  add_dependencies(aivfs_e2e AiVfsLauncher aivfs_probe)
  add_test(NAME e2e COMMAND aivfs_e2e $<TARGET_FILE:AiVfsLauncher> $<TARGET_FILE:aivfs_probe>)
endif()

install(TARGETS AiVfsLauncher RUNTIME DESTINATION .)
install(FILES ${USVFS_RUNTIME_FILES} LICENSE DESTINATION .)
