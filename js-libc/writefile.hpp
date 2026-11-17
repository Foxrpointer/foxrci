#pragma once
#include "quickjs.h"

// 文件写入宿主模块：注册 WriteFile(path, content) 全局函数。
// 语义：覆盖写（存在则整体替换，不存在则创建），二进制精确写入。
namespace writefile_host {

    void register_globals(JSContext *ctx);

}
