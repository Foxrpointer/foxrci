#include "parser.hpp"
#include "module_loader.hpp"
#include <fstream>
#include <sstream>

Parser::Parser() {
    rt_ = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);

    // 向全局注册所有动态库模块提供的函数/类。
    // 失败信息写进 pkg，这里只记录、不中断启动。
    Commands::CommandRetrunPackage pkg;
    pkg.history_commands.push_back("parser:ctor");
    module_loader::register_all(ctx_, pkg);
}

Parser::~Parser() {
    JS_FreeContext(ctx_);
    JS_FreeRuntime(rt_);
}

bool Parser::run_js_file(const std::string &path, Commands::CommandRetrunPackage &pkg) {
    pkg.history_commands.push_back(path);

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        pkg.error = "cannot open file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string src = ss.str();

    JSValue val = JS_Eval(ctx_, src.c_str(), src.size(), path.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue err = JS_GetException(ctx_);
        const char *msg = JS_ToCString(ctx_, err);
        pkg.error = msg ? msg : "unknown JS exception";
        JS_FreeCString(ctx_, msg);
        JS_FreeValue(ctx_, err);
        JS_FreeValue(ctx_, val);
        return false;
    }
    JS_FreeValue(ctx_, val);
    return true;
}
