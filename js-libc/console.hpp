#pragma once
#include "quickjs.h"

// console 宿主模块：注册 console_log / console_info / console_warn / console_error
// 四个全局函数，带时间戳与 ANSI 颜色。JS 层自行组装成 console 类。
namespace console_host {

    // 在 JSContext 创建之后、执行任何脚本之前调用一次
    void register_globals(JSContext *ctx);

}
