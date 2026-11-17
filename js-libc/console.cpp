#include "console.hpp"
#include <cstdio>
#include <ctime>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace console_host {
namespace {  // 内部实现，不对外可见

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

// ---------- 胶水层 ----------

// 四个级别共用一个实现，靠 magic（JS_NewCFunction 的最后一个参数）区分级别
JSValue js_console(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int magic) {
    if (magic < 0 || magic > 3) return JS_ThrowInternalError(ctx, "console: bad magic");
    const LevelStyle &style = kStyles[magic];

    // 多个参数用空格拼接，与浏览器 console.log(a, b) 行为一致
    std::string line;
    for (int i = 0; i < argc; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) return JS_EXCEPTION;   // 转换失败，异常已设置
        if (i) line += ' ';
        line += s;
        JS_FreeCString(ctx, s);
    }
    emit_line(style, line.c_str());
    return JS_UNDEFINED;
}

JSValue js_console_log  (JSContext *c, JSValueConst t, int n, JSValueConst *a) { return js_console(c, t, n, a, 0); }
JSValue js_console_info (JSContext *c, JSValueConst t, int n, JSValueConst *a) { return js_console(c, t, n, a, 1); }
JSValue js_console_warn (JSContext *c, JSValueConst t, int n, JSValueConst *a) { return js_console(c, t, n, a, 2); }
JSValue js_console_error(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return js_console(c, t, n, a, 3); }

} // namespace

void register_globals(JSContext *ctx) {
    enable_ansi_color();   // Windows 上先开 VT 模式，再注册函数
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "console_log",   JS_NewCFunction(ctx, js_console_log,   "console_log",   1));
    JS_SetPropertyStr(ctx, g, "console_info",  JS_NewCFunction(ctx, js_console_info,  "console_info",  1));
    JS_SetPropertyStr(ctx, g, "console_warn",  JS_NewCFunction(ctx, js_console_warn,  "console_warn",  1));
    JS_SetPropertyStr(ctx, g, "console_error", JS_NewCFunction(ctx, js_console_error, "console_error", 1));
    JS_FreeValue(ctx, g);
}

} // namespace console_host
