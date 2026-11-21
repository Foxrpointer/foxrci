#pragma once
#include "quickjs.h"

// console 动态库模块：向 JS 全局注册 console 类。
// 类在 module.cpp 内部实现，对外只暴露 module_abi 里的两个导出函数。
