#pragma once
#include "commands/package.hpp"
#include "quickjs.h"

// 动态库模块 ABI：每个 foxrci 模块动态库必须导出这两个函数。
//
//   foxrci_module_extract   —— 在 init.cpp（foxrci --init）里调用，
//                             用于向磁盘写入前置 js 文件。目前没有，
//                             各模块可以直接 return true。
//   foxrci_module_register  —— 在 module_loader.cpp 里调用，
//                             用于向全局注册函数/类；出错把信息写进 pkg。
//
// 两个函数都透传 CommandReturnPackage，把可能存在的错误数据抬出来。
// 统一用 extern "C" 导出，方便 GetProcAddress 按名字查找。

extern "C" {

// 返回 false 表示提取失败，错误信息写进 pkg.error。
typedef bool (*foxrci_module_extract_fn)(Commands::CommandRetrunPackage &pkg);

// 返回 false 表示注册失败，错误信息写进 pkg.error。
typedef bool (*foxrci_module_register_fn)(JSContext *ctx,
                                          Commands::CommandRetrunPackage &pkg);

} // extern "C"
