#include "writefile.hpp"
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace writefile_host {
namespace {

// ---------- 核心层 ----------

bool write_file(const std::string &path, const std::string &content) {
#ifdef _WIN32
    // Win32 窄字符 API 按 ANSI(GBK) 解释路径，中文路径会炸：
    // UTF-8 → UTF-16 转换后用 _wfopen
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath((size_t)wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    FILE *f = _wfopen(wpath.c_str(), L"wb");
#else
    FILE *f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    bool ok = std::fwrite(content.data(), 1, content.size(), f) == content.size();
    std::fclose(f);
    return ok;
}

// ---------- 胶水层 ----------

JSValue js_write_file(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "WriteFile: expected (path, content)");

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    const char *content = JS_ToCString(ctx, argv[1]);
    if (!content) { JS_FreeCString(ctx, path); return JS_EXCEPTION; }

    bool ok = write_file(path, content);

    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, content);
    return JS_NewBool(ctx, ok);
}

} // namespace

void register_globals(JSContext *ctx) {
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "WriteFile",
                      JS_NewCFunction(ctx, js_write_file, "WriteFile", 2));
    JS_FreeValue(ctx, g);
}

} // namespace writefile_host
