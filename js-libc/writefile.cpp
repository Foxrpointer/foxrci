#include "writefile.hpp"
#include <cstdio>
#include <unistd.h>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

namespace writefile_host {
namespace {

// ---------- 核心层 ----------

bool write_file(const std::string &path, const std::string &content, bool when_file_exists_stop)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath((size_t)wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    bool exists = (_waccess(wpath.c_str(), 0) == 0);
#else
    bool exists = (access(path.c_str(), F_OK) == 0);
#endif

    // content 为空字符串 => 只检测文件存在 / 删除文件
    if (content.empty()) {
        if (when_file_exists_stop) {
            // 文件存在 => 返回 false
            return !exists;
        } else {
            // 文件存在 => 删除文件
            if (exists) {
#ifdef _WIN32
                return _wremove(wpath.c_str()) == 0;
#else
                return std::remove(path.c_str()) == 0;
#endif
            }
            return true;
        }
    }

    // content 非空 => 正常写入
    if (when_file_exists_stop && exists) return false;

#ifdef _WIN32
    FILE *f = _wfopen(wpath.c_str(), L"wb");
#else
    FILE *f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return false;
    bool ok = std::fwrite(content.data(), 1, content.size(), f) == content.size();
    std::fclose(f);
    return ok;
}

JSValue js_write_file(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "WriteFile: expected (path, content, [when_file_exists_stop])");

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    const char *content = JS_ToCString(ctx, argv[1]);
    if (!content) { JS_FreeCString(ctx, path); return JS_EXCEPTION; }

    bool when_file_exists_stop = (argc >= 3) ? JS_ToBool(ctx, argv[2]) : true;

    bool ok = write_file(path, content, when_file_exists_stop);

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
