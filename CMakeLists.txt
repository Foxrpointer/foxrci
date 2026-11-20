cmake_minimum_required(VERSION 3.20)
project(foxrci VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party/quickjs)

set(SOURCES
  foxrci.cpp
)

# quickjs 引入
add_library(quickjs STATIC
  ${QUICKJS_DIR}/quickjs.c
  ${QUICKJS_DIR}/libregexp.c
  ${QUICKJS_DIR}/libunicode.c
  ${QUICKJS_DIR}/cutils.c
  ${QUICKJS_DIR}/dtoa.c
)

target_include_directories(quickjs PUBLIC
  ${QUICKJS_DIR}
)

add_executable(foxrci
  foxrci.cpp
  init.cpp
  parser/parser.cpp
  parser/js_runtime_init_clean.cpp
  parser/module_loader.cpp

  js-libc/writefile.cpp
  js-libc/cmdapply.cpp
  js-libc/console.cpp
)

target_include_directories(foxrci PRIVATE
  ${QUICKJS_DIR}
)

target_compile_definitions(quickjs PRIVATE CONFIG_VERSION="2026-06-04")

target_link_libraries(foxrci PRIVATE quickjs pthread)
