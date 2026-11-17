#ifndef COMMANDS_RUN_JS_HPP
#define COMMANDS_RUN_JS_HPP

#include <string>
#include <quickjs.h>
#include "init.hpp"

namespace Commands
{

// QuickJS 上下文管理：首次调用时创建，程序退出前手动释放
void js_runtime_init();
void js_runtime_cleanup();

// 在已有上下文中执行 JS 文件
bool run_js_file(const std::string& filepath, CommandRetrunPackage& pkg);

}

#endif
