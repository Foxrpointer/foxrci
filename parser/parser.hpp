#pragma once
#include "../commands/package.hpp"
#include "quickjs.h"

// JS 运行时宿主：持有整个程序共享的 JSContext。
// 创建 Context 时完成内置全局函数/类的注册，之后可反复 run_js_file。
class Parser {
public:
    Parser();
    ~Parser();

    Parser(const Parser &) = delete;
    Parser &operator=(const Parser &) = delete;

    // 在共享上下文里执行一个 JS 文件。失败时把信息写进 pkg.error，
    // 并把命令名压进 pkg.history_commands。
    bool run_js_file(const std::string &path, Commands::CommandRetrunPackage &pkg);

    JSContext *context() const { return ctx_; }

private:
    JSRuntime *rt_ = nullptr;
    JSContext *ctx_ = nullptr;
};
