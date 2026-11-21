#include "module.hpp"
#include "../../module_abi.hpp"
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace file_host {
namespace {   // File 类在本 .cpp 内部实现，不对外可见

// ---------- 核心层 ----------

bool path_exists(const std::string &path) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath((size_t)wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    return _waccess(wpath.c_str(), 0) == 0;
#else
    return access(path.c_str(), F_OK) == 0;
#endif
}

bool path_remove(const std::string &path) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath((size_t)wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    return _wremove(wpath.c_str()) == 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

bool write_file(const std::string &path, const std::string &content) {
#ifdef _WIN32
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

// ---------- JS File 类 ----------

// JS File 实例 ↔ C++ 状态
struct FileState {
    std::string filepath;
    std::string content;   // write_line / write_content 累积到这里，commit 时才落盘
};

JSClassID g_file_class_id;

void file_finalizer(JSRuntime *, JSValue val) {
    auto *st = static_cast<FileState *>(JS_GetOpaque(val, g_file_class_id));
    delete st;
}

JSValue file_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    auto *st = new FileState();
    if (argc >= 1) {
        const char *p = JS_ToCString(ctx, argv[0]);
        if (p) { st->filepath = p; JS_FreeCString(ctx, p); }
    }
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    JSValue obj = JS_NewObjectProtoClass(ctx, proto, g_file_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj)) { delete st; return obj; }
    JS_SetOpaque(obj, st);
    return obj;
}

JSValue js_file_exists(JSContext *ctx, JSValueConst this_val, int, JSValueConst *) {
    auto *st = static_cast<FileState *>(JS_GetOpaque(this_val, g_file_class_id));
    if (!st) return JS_ThrowTypeError(ctx, "File: bad this");
    return JS_NewBool(ctx, path_exists(st->filepath));
}

JSValue js_file_del(JSContext *ctx, JSValueConst this_val, int, JSValueConst *) {
    auto *st = static_cast<FileState *>(JS_GetOpaque(this_val, g_file_class_id));
    if (!st) return JS_ThrowTypeError(ctx, "File: bad this");
    return JS_NewBool(ctx, path_remove(st->filepath));
}

// 立即覆盖写。注意：不要传空 content
JSValue js_file_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *st = static_cast<FileState *>(JS_GetOpaque(this_val, g_file_class_id));
    if (!st) return JS_ThrowTypeError(ctx, "File: bad this");
    if (argc < 1) return JS_ThrowTypeError(ctx, "File.write: expected (content)");
    const char *c = JS_ToCString(ctx, argv[0]);
    if (!c) return JS_EXCEPTION;
    bool ok = write_file(st->filepath, c);
    JS_FreeCString(ctx, c);
    return JS_NewBool(ctx, ok);
}

JSValue js_file_write_content(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *st = static_cast<FileState *>(JS_GetOpaque(this_val, g_file_class_id));
    if (!st) return JS_ThrowTypeError(ctx, "File: bad this");
    if (argc < 1) return JS_ThrowTypeError(ctx, "File.write_content: expected (content)");
    const char *c = JS_ToCString(ctx, argv[0]);
    if (!c) return JS_EXCEPTION;
    st->content = c;
    JS_FreeCString(ctx, c);
    return JS_UNDEFINED;
}

JSValue js_file_write_line(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    auto *st = static_cast<FileState *>(JS_GetOpaque(this_val, g_file_class_id));
    if (!st) return JS_ThrowTypeError(ctx, "File: bad this");
    if (argc < 1) return JS_ThrowTypeError(ctx, "File.write_line: expected (text)");
    const char *c = JS_ToCString(ctx, argv[0]);
    if (!c) return JS_EXCEPTION;
    st->content += c;
    st->content += '\n';
    JS_FreeCString(ctx, c);
    return JS_UNDEFINED;
}

// 把累积的 content 落盘
JSValue js_file_commit(JSContext *ctx, JSValueConst this_val, int, JSValueConst *) {
    auto *st = static_cast<FileState *>(JS_GetOpaque(this_val, g_file_class_id));
    if (!st) return JS_ThrowTypeError(ctx, "File: bad this");
    return JS_NewBool(ctx, write_file(st->filepath, st->content));
}

const JSCFunctionListEntry file_proto_funcs[] = {
    JS_CFUNC_DEF("file_exists",   0, js_file_exists),
    JS_CFUNC_DEF("del",           0, js_file_del),
    JS_CFUNC_DEF("write",         1, js_file_write),
    JS_CFUNC_DEF("write_content", 1, js_file_write_content),
    JS_CFUNC_DEF("write_line",    1, js_file_write_line),
    JS_CFUNC_DEF("commit",        0, js_file_commit),
};

} // namespace

void register_globals(JSContext *ctx) {
    JSClassDef def{
        "File",
        file_finalizer,
    };
    JS_NewClassID(&g_file_class_id);
    JS_NewClass(JS_GetRuntime(ctx), g_file_class_id, &def);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, file_proto_funcs,
                               (int)(sizeof file_proto_funcs / sizeof *file_proto_funcs));

    JSValue ctor = JS_NewCFunction2(ctx, file_ctor, "File", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);

    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "File", ctor);
    JS_FreeValue(ctx, g);
    JS_FreeValue(ctx, proto);
}

} // namespace file_host

// ---------- 动态库导出 ----------

extern "C" __declspec(dllexport)
bool foxrci_module_extract(Commands::CommandRetrunPackage &) {
    // File 模块没有需要落盘的前置 js 文件
    return true;
}

extern "C" __declspec(dllexport)
bool foxrci_module_register(JSContext *ctx, Commands::CommandRetrunPackage &pkg) {
    pkg.history_commands.push_back("module:file.register");
    file_host::register_globals(ctx);
    return true;
}
