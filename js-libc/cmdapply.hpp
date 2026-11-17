#pragma once
#include "quickjs.h"

// 进程执行宿主模块：注册 CmdApply(path, args[]) 全局函数。
// 同步执行，内存捕获输出，返回 { code, stdout, stderr }。
// 注意：不经 shell，因此不支持 | > 通配符；args 元素无需转义。
namespace cmdapply_host {

    void register_globals(JSContext *ctx);

}
