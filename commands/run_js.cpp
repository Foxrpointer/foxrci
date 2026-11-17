#include "run_js.hpp"

#include <string>
#include <fstream>
#include <filesystem>
#include <quickjs.h>

#include "../js-libc/cmdapply.hpp"
#include "../js-libc/console.hpp"
#include "../js-libc/writefile.hpp"

namespace Commands
{
    namespace fs = std::filesystem;

    static JSRuntime* g_rt = nullptr;
    static JSContext* g_ctx = nullptr;

    // 1. 模块加载器：遇到 import 时调用
    static JSModuleDef* module_loader(
        JSContext* ctx,
        const char* module_name,
        void* opaque)
    {
        std::ifstream ifs(module_name);
        if (!ifs) {
            JS_ThrowReferenceError(ctx, "Cannot find module: %s", module_name);
            return nullptr;
        }
        std::string source(
            (std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>()
        );

        // 用 COMPILE_ONLY 只编译不执行，避免 JS_Eval 自动设置 attributes
        JSValue val = JS_Eval(
            ctx,
            source.c_str(),
            source.size(),
            module_name,
            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY
        );

        if (JS_IsException(val)) {
            JS_FreeValue(ctx, val);
            return nullptr;
        }

        // 提取模块定义，然后释放 JSValue（模块已被 loaded_modules 持有）
        JSModuleDef* m = (JSModuleDef*)JS_VALUE_GET_PTR(val);
        JS_FreeValue(ctx, val);
        return m;
    }

    // 2. 模块路径解析器：把相对路径解析为绝对路径，确保同一文件只加载一次
    static char* module_normalize(
        JSContext* ctx,
        const char* base_name,
        const char* name,
        void* opaque)
    {
        fs::path base = fs::path(base_name).parent_path();
        fs::path full = (base / name).lexically_normal();
        // 转为绝对路径，避免同一文件因相对路径不同被重复加载
        std::error_code ec;
        fs::path abs = fs::absolute(full, ec);
        if (!ec) full = abs.lexically_normal();
        return js_strdup(ctx, full.string().c_str());
    }

    // 初始化 QuickJS 运行时（只调一次）
    void js_runtime_init()
    {
        if (g_rt) return;
        g_rt = JS_NewRuntime();
        g_ctx = JS_NewContext(g_rt);
        // 全局函数注册
        console_host::register_globals(g_ctx);
        writefile_host::register_globals(g_ctx);
        cmdapply_host::register_globals(g_ctx);
        JS_SetModuleLoaderFunc(g_rt, module_normalize, module_loader, nullptr);
    }

    // 清理 QuickJS 运行时
    void js_runtime_cleanup()
    {
        if (g_ctx) { JS_FreeContext(g_ctx); g_ctx = nullptr; }
        if (g_rt)  { JS_FreeRuntime(g_rt);  g_rt  = nullptr; }
    }

    // 在已有上下文中执行 JS 文件
    bool run_js_file(const std::string& filepath, CommandRetrunPackage& pkg)
    {
        JSContext* ctx = g_ctx;
        if (!ctx) {
            pkg.has_error = true;
            pkg.error = "JS runtime not initialized";
            return false;
        }

        // 如果上一条命令出错，跳过
        if (pkg.has_error) return false;
        pkg.history_commands.push_back(filepath);

        std::ifstream ifs(filepath);
        if (!ifs) {
            pkg.has_error = true;
            pkg.error = "Cannot open file: " + filepath;
            return false;
        }
        std::string source(
            (std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>()
        );

        JSValue val = JS_Eval(
            ctx,
            source.c_str(),
            source.size(),
            filepath.c_str(),
            JS_EVAL_TYPE_MODULE
        );

        if (JS_IsException(val)) {
            JSValue exc = JS_GetException(ctx);
            const char* err = JS_ToCString(ctx, exc);
            pkg.has_error = true;
            pkg.error = err ? err : "unknown JS error";
            JS_FreeCString(ctx, err);
            JS_FreeValue(ctx, exc);
            JS_FreeValue(ctx, val);
            return false;
        }

        JS_FreeValue(ctx, val);

        // 执行 pending 的 Promise / async 回调
        JSContext* pctx;
        while (JS_ExecutePendingJob(g_rt, &pctx) > 0) {}

        return true;
    }
}
