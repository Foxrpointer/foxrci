#include "cmdapply.hpp"
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <thread>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#endif

namespace cmdapply_host {
namespace {

// ---------- 核心层 ----------

struct ExecResult {
    int code = -1;
    std::string out;
    std::string err;
};

#ifdef _WIN32

std::wstring utf8_to_utf16(const std::string &s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w((size_t)len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

std::wstring quote_arg(const std::wstring &a) {
    // CreateProcess 的 cmdline 是一个字符串，参数要自己加引号并转义
    std::wstring q = L"\"";
    for (wchar_t c : a) {
        if (c == L'"') q += L"\\\"";
        else q += c;
    }
    q += L'"';
    return q;
}

ExecResult exec_wait(const std::string &path, const std::vector<std::string> &args) {
    ExecResult r;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_r, out_w, err_r, err_w;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return r;
    if (!CreatePipe(&err_r, &err_w, &sa, 0)) {
        CloseHandle(out_r); CloseHandle(out_w); return r;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);  // 读端不继承
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmdline = quote_arg(utf8_to_utf16(path));
    for (const auto &a : args)
        cmdline += L" " + quote_arg(utf8_to_utf16(a));

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError  = err_w;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr,
                             TRUE, 0, nullptr, nullptr, &si, &pi);
    CloseHandle(out_w);   // 父进程关掉写端，读端才会读到 EOF
    CloseHandle(err_w);
    if (!ok) { CloseHandle(out_r); CloseHandle(err_r); return r; }

    // 两个线程分别读两条管道（Windows 管道不并发读会死锁）。
    // 线程只碰纯 C++ 的 std::string，绝不触碰 JS 对象 —— 符合单 context 规则。
    auto reader = [](HANDLE h, std::string *dst) {
        char buf[8192]; DWORD n;
        while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0)
            dst->append(buf, n);
        CloseHandle(h);
    };
    std::thread t1(reader, out_r, &r.out);
    std::thread t2(reader, err_r, &r.err);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.code = (int)code;

    t1.join(); t2.join();
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return r;
}

#else  // POSIX

ExecResult exec_wait(const std::string &path, const std::vector<std::string> &args) {
    ExecResult r;
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return r;

    pid_t pid = fork();
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(path.c_str()));
        for (const auto &a : args)
            argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        execvp(path.c_str(), argv.data());
        _exit(127);   // exec 失败（命令不存在等）
    }
    close(out_pipe[1]); close(err_pipe[1]);

    // 用 poll 同时读两条管道：只顺序读的话，stderr 写满缓冲区会死锁
    char buf[8192];
    int fds[2]  = {out_pipe[0], err_pipe[0]};
    bool open[2] = {true, true};
    while (open[0] || open[1]) {
        pollfd pfds[2]; int map[2]; int cnt = 0;
        for (int i = 0; i < 2; i++)
            if (open[i]) { pfds[cnt] = {fds[i], POLLIN, 0}; map[cnt++] = i; }
        if (poll(pfds, cnt, -1) < 0) break;
        for (int i = 0; i < cnt; i++) {
            short re = pfds[i].revents;
            if (re & (POLLIN | POLLHUP)) {
                ssize_t n = read(fds[map[i]], buf, sizeof buf);
                if (n > 0) (map[i] == 0 ? r.out : r.err).append(buf, (size_t)n);
                else open[map[i]] = false;
            } else if (re & (POLLERR | POLLNVAL)) {
                open[map[i]] = false;
            }
        }
    }
    close(out_pipe[0]); close(err_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

#endif

// ---------- 胶水层 ----------

// JSValue 数组 → std::vector<std::string>
bool js_to_str_vec(JSContext *ctx, JSValueConst v, std::vector<std::string> &out) {
    JSValue len_v = JS_GetPropertyStr(ctx, v, "length");
    if (JS_IsException(len_v)) return false;
    int64_t len = 0;
    JS_ToInt64(ctx, &len, len_v);
    JS_FreeValue(ctx, len_v);
    for (int64_t i = 0; i < len; i++) {
        JSValue e = JS_GetPropertyUint32(ctx, v, (uint32_t)i);
        if (JS_IsException(e)) return false;
        const char *s = JS_ToCString(ctx, e);
        if (!s) { JS_FreeValue(ctx, e); return false; }
        out.emplace_back(s);
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, e);
    }
    return true;
}

JSValue js_cmd_apply(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "CmdApply: expected (path, args[])");

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;

    std::vector<std::string> args;
    if (!js_to_str_vec(ctx, argv[1], args)) {   // 失败时异常已设置
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    ExecResult r = exec_wait(path, args);
    JS_FreeCString(ctx, path);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "code",   JS_NewInt32(ctx, r.code));
    JS_SetPropertyStr(ctx, obj, "stdout", JS_NewString(ctx, r.out.c_str()));
    JS_SetPropertyStr(ctx, obj, "stderr", JS_NewString(ctx, r.err.c_str()));
    return obj;
}

} // namespace

void register_globals(JSContext *ctx) {
    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "CmdApply",
                      JS_NewCFunction(ctx, js_cmd_apply, "CmdApply", 2));
    JS_FreeValue(ctx, g);
}

} // namespace cmdapply_host
