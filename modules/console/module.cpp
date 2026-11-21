#include "module.hpp"
#include "../../module_abi.hpp"
#include <cstdio>
#include <ctime>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace console_host {
namespace {   // 类在本 .cpp 内部实现，不对外可见

// ---------- 核心层（纯 C++，不认识 JS） ----------

struct LevelStyle {
    const char *name;   // 级别名，打印在方括号里
    const char *color;  // ANSI 颜色码
    bool use_stderr;    // error 走 stderr，其余走 stdout
};

constexpr LevelStyle kStyles[] = {
    /* log   */ {"log",   "37", false},   // 白
    /* info  */ {"info",  "36", false},   // 青
    /* warn  */ {"warn",  "33", false},   // 黄
    /* error */ {"error", "31", true },   // 红
};

void enable_ansi_color() {
#ifdef _WIN32
    // Windows 10+ 支持 ANSI 颜色，但默认关闭，需要手动开启 VT 模式
    for (DWORD id : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        HANDLE h = GetStdHandle(id);
        if (h == nullptr || h == INVALID_HANDLE_VALUE) continue;
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
            SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

void emit_line(const LevelStyle &style, const char *msg) {
    char ts[16];
    std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    std::strftime(ts, sizeof ts, "%H:%M:%S", &tm_buf);

    FILE *out = style.use_stderr ? stderr : stdout;
    std::fprintf(out, "\033[%sm[%s] [%s] %s\033[0m\n",
                 style.color, ts, style.name, msg);
    std::fflush(out);
}

// ---------- console 类 ----------

// 四个级别共用一个实现，靠 magic（JS 类方法的 func_data）区分级别
JSValue console_method(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv, int magic) {
    if (magic < 0 || magic > 3) return JS_ThrowInternalError(ctx, "console: bad magic");
    const LevelStyle &style = kStyles[magic];

    std::string line;
    for (int i = 0; i < argc; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) return JS_EXCEPTION;
        if (i) line += ' ';
        line += s;
        JS_FreeCString(ctx, s);
    }
    emit_line(style, line.c_str());
    return JS_UNDEFINED;
}

JSValue console_ctor(JSContext *ctx, JSValueConst, int, JSValueConst *) {
    return JS_NewObject(ctx);
}

JSValue console_log  (JSContext *c, JSValueConst t, int n, JSValueConst *a) { return console_method(c, t, n, a, 0); }
JSValue console_info (JSContext *c, JSValueConst t, int n, JSValueConst *a) { return console_method(c, t, n, a, 1); }
JSValue console_warn (JSContext *c, JSValueConst t, int n, JSValueConst *a) { return console_method(c, t, n, a, 2); }
JSValue console_error(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return console_method(c, t, n, a, 3); }

const JSCFunctionListEntry console_proto_funcs[] = {
    JS_CFUNC_DEF("log",   1, console_log),
    JS_CFUNC_DEF("info",  1, console_info),
    JS_CFUNC_DEF("warn",  1, console_warn),
    JS_CFUNC_DEF("error", 1, console_error),
};

} // namespace

void register_globals(JSContext *ctx) {
    enable_ansi_color();   // Windows 上先开 VT 模式，再注册类

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, console_proto_funcs,
                               (int)(sizeof console_proto_funcs / sizeof *console_proto_funcs));

    JSValue ctor = JS_NewCFunction2(ctx, console_ctor, "console", 0, JS_CFUNC_constructor, 0);
    // console 作为“静态类”使用：方法直接挂在构造器上，new 也不报错
    JS_SetPropertyFunctionList(ctx, ctor, console_proto_funcs,
                               (int)(sizeof console_proto_funcs / sizeof *console_proto_funcs));
    JS_SetConstructor(ctx, ctor, proto);

    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "console", ctor);
    JS_FreeValue(ctx, g);
    JS_FreeValue(ctx, proto);
}

} // namespace console_host

// ---------- 动态库导出 ----------

extern "C" __declspec(dllexport)
bool foxrci_module_extract(Commands::CommandRetrunPackage &) {
    // console 模块没有需要落盘的前置 js 文件
    return true;
}

extern "C" __declspec(dllexport)
bool foxrci_module_register(JSContext *ctx, Commands::CommandRetrunPackage &pkg) {
    pkg.history_commands.push_back("module:console.register");
    console_host::register_globals(ctx);
    return true;
}
