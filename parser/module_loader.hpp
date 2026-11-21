#pragma once
#include "../commands/package.hpp"
#include "quickjs.h"
#include <string>

namespace module_loader {

// 遍历 dir 下的动态库并加载（取出 extract/register 两个入口）。
bool load_all(const std::string &dir, Commands::CommandRetrunPackage &pkg);

// 依次调用各模块的 extract（foxrci --init 时执行）。
bool extract_all(Commands::CommandRetrunPackage &pkg);

// 依次调用各模块的 register，向全局函数/类注册。
bool register_all(JSContext *ctx, Commands::CommandRetrunPackage &pkg);

} // namespace module_loader
