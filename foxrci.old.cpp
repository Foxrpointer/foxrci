// FoxrCI — CI 驱动核心
//
// 读取 build.js，用 QuickJS 执行；约定脚本里定义 `class Main`，其构造函数
// 接收一个 `ci` 对象作为唯一参数，构造函数即整个配置过程。
//
// build.js 里通过 `let cp = ci.create_project()` 得到一个可链式配置的工程
// 对象，最后调用 `cp.commit()` 提交并「立即执行」。一次 commit 对应一次
// 执行阶段：调用两次即可先编译（并生成 compile_commands.json）再链接。
//
// 后端：ci.backend("gcc" | "ninja")，缺省读环境变量 FOXRCI_BACKEND，
// 再缺省为 "gcc"（直出）。工具链从 CC / CXX 读取，缺省 gcc / g++。

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#include "quickjs.h"

namespace {

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------

struct ProjectState {
    std::string type = "exe";     // "exe" | "lib" | "obj"
    std::string name;
    std::vector<std::string> sources;
    std::vector<std::string> include;
    std::vector<std::string> define;
    std::vector<std::string> link;
    std::vector<std::string> linkFlags;
    std::vector<std::string> cxxFlags;

    int commitCount = 0;          // 已提交次数：0 未提交，1 编译，2 链接
    bool compiled = false;
};

struct Engine {
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    ProjectState* proj = nullptr;   // 指向当前正在配置的工程
    std::string backend = "gcc";
    std::string root;               // 工作目录
    std::string cc, cxx;
    int lastExit = 0;
};

Engine* g_engine = nullptr;

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------

std::string GetEnv(const char* name, const char* fallback) {
    char buf[4096];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return fallback;
    return std::string(buf);
}

std::string CurrentDir() {
    char buf[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(MAX_PATH, buf);
    return n ? std::string(buf, n) : std::string(".");
}

// 把路径分隔符统一为 '/'
std::string Slash(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

std::string ReadFileText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string Join(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += sep;
        s += v[i];
    }
    return s;
}

// 命令行参数转义（Windows CreateProcess 规则）
std::string QuoteArg(const std::string& a) {
    if (a.empty()) return "\"\"";
    bool need = false;
    for (char c : a) if (c == ' ' || c == '\t' || c == '"') { need = true; break; }
    if (!need) return a;
    std::string out = "\"";
    int backslashes = 0;
    for (char c : a) {
        if (c == '\\') { ++backslashes; }
        else if (c == '"') { out.append(backslashes * 2 + 1, '\\'); out += '"'; backslashes = 0; }
        else { out.append(backslashes, '\\'); backslashes = 0; out += c; }
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

// 运行一个进程，返回退出码。capture 非空时把 stdout+stderr 写入文件。
int RunProcess(const std::string& cmdline, const std::string& captureFile) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr, wr = nullptr;
    if (!captureFile.empty()) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr ? wr : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = wr ? wr : GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi{};
    std::vector<char> mutableCmd(cmdline.begin(), cmdline.end());
    mutableCmd.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             0, nullptr, nullptr, &si, &pi);
    if (!ok) return -1;

    CloseHandle(pi.hThread);

    std::string captured;
    if (rd) {
        char buf[4096];
        DWORD got = 0;
        while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0)
            captured.append(buf, got);
        CloseHandle(rd);
        CloseHandle(wr);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);

    if (!captureFile.empty()) {
        std::ofstream f(captureFile, std::ios::binary);
        f << captured;
    }
    return static_cast<int>(code);
}

// 控制台输出（GUI 子系统下没有控制台，额外往 stdout 写信也算尽力而为）
void Log(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// compile_commands.json 生成
// ---------------------------------------------------------------------------

struct CompileCommand {
    std::string file;
    std::string command;
    std::string output;
};

void WriteCompileCommands(const Engine& e, const ProjectState& p,
                          const std::vector<CompileCommand>& cmds) {
    std::string path = e.root + "/compile_commands.json";
    std::ofstream f(path, std::ios::binary);
    f << "[\n";
    for (size_t i = 0; i < cmds.size(); ++i) {
        f << "  {\n";
        f << "    \"directory\": \"" << Slash(e.root) << "\",\n";
        f << "    \"file\": \"" << Slash(cmds[i].file) << "\",\n";
        f << "    \"command\": \"" << Slash(cmds[i].command) << "\",\n";
        f << "    \"output\": \"" << Slash(cmds[i].output) << "\"\n";
        f << "  }" << (i + 1 < cmds.size() ? "," : "") << "\n";
    }
    f << "]\n";
    Log("[foxrci] wrote " + Slash(path));
    (void)p;
}

// 为单个源文件构造编译命令
std::string BuildCompileCmd(const Engine& e, const ProjectState& p,
                            const std::string& src, const std::string& obj) {
    std::string c = QuoteArg(e.cxx);
    for (auto& d : p.define)   c += " -D" + QuoteArg(d);
    for (auto& inc : p.include) c += " -I" + QuoteArg(inc);
    for (auto& fl : p.cxxFlags) c += " " + fl;
    c += " -c " + QuoteArg(src) + " -o " + QuoteArg(obj);
    return c;
}

std::string BuildLinkCmd(const Engine& e, const ProjectState& p,
                         const std::vector<std::string>& objs) {
    std::string c = QuoteArg(e.cxx);
    if (p.type == "exe") c += " -mwindows";  // GUI 子系统
    for (auto& o : objs) c += " " + QuoteArg(o);
    for (auto& lf : p.linkFlags) c += " " + lf;
    for (auto& l : p.link) c += " -l" + QuoteArg(l);
    c += " -o " + QuoteArg(p.name + ".exe");
    return c;
}

// obj 文件的输出路径
std::string ObjPath(const ProjectState& p, const std::string& src) {
    std::string base = Slash(src);
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return "obj/" + base + ".o";
}

// ---------------------------------------------------------------------------
// 执行：commit
// ---------------------------------------------------------------------------

// 生成 build.ninja（ninja 后端）
void EmitNinja(const Engine& e, const ProjectState& p,
               const std::vector<CompileCommand>& cmds) {
    std::ofstream f(e.root + "/build.ninja", std::ios::binary);
    f << "# generated by foxrci\n\n";
    f << "cxx = " << Slash(e.cxx) << "\n";
    f << "cflags =";
    for (auto& d : p.define) f << " -D" << d;
    for (auto& inc : p.include) f << " -I" << Slash(inc);
    for (auto& fl : p.cxxFlags) f << " " << fl;
    f << "\n\n";

    f << "rule cc\n  command = $cxx $cflags -c $in -o $out\n";
    f << "rule link\n  command = $cxx " << (p.type == "exe" ? "-mwindows " : "")
      << "$in";
    for (auto& lf : p.linkFlags) f << " " << lf;
    for (auto& l : p.link) f << " -l" << l;
    f << " -o $out\n\n";

    for (auto& cc : cmds)
        f << "build " << Slash(cc.output) << ": cc " << Slash(cc.file) << "\n";

    std::string objs;
    for (auto& cc : cmds) objs += " " + Slash(cc.output);
    f << "\nbuild " << p.name << ".exe: link" << objs << "\n";
    f << "\ndefault " << p.name << ".exe\n";
    Log("[foxrci] wrote " + Slash(e.root + "/build.ninja"));
}

// 一次编译（生成 compile_commands.json + 编每个源文件）
void DoCompileStep(Engine& e, ProjectState& p) {
    CreateDirectoryA("obj", nullptr);

    std::vector<CompileCommand> cmds;
    for (auto& src : p.sources) {
        CompileCommand cc;
        cc.file = src;
        cc.output = ObjPath(p, src);
        cc.command = BuildCompileCmd(e, p, src, cc.output);
        cmds.push_back(cc);
    }
    WriteCompileCommands(e, p, cmds);

    if (e.backend == "ninja") {
        EmitNinja(e, p, cmds);
        return;  // 交给 ninja 执行
    }

    // gcc 直出：逐个编译
    for (auto& cc : cmds) {
        Log("[foxrci] compile " + cc.file);
        int rc = RunProcess(cc.command, {});
        if (rc != 0) {
            Log("[foxrci] compile failed: " + cc.file);
            e.lastExit = 1;
            return;
        }
    }
    p.compiled = true;
}

// 一次链接
void DoLinkStep(Engine& e, ProjectState& p) {
    std::vector<std::string> objs;
    for (auto& src : p.sources) objs.push_back(ObjPath(p, src));

    std::string cmd = BuildLinkCmd(e, p, objs);
    Log("[foxrci] link -> " + p.name + ".exe");
    int rc = RunProcess(cmd, {});
    if (rc != 0) {
        Log("[foxrci] link failed");
        e.lastExit = 1;
    }
}

// ---------------------------------------------------------------------------
// QuickJS 绑定：Project
// ---------------------------------------------------------------------------

JSClassID g_project_class = 0;

struct JsProject {
    ProjectState state;   // 每个 cp 拥有自己的状态快照
};

JsProject* GetProject(JSContext* ctx, JSValueConst v) {
    return static_cast<JsProject*>(JS_GetOpaque2(ctx, v, g_project_class));
}

// 往 std::vector<std::string> 追加一个字符串或字符串数组/或可变参数
bool AppendStrings(JSContext* ctx, JSValueConst val, std::vector<std::string>& out) {
    if (JS_IsArray(ctx, val)) {
        JSValue lenV = JS_GetPropertyStr(ctx, val, "length");
        int32_t len = 0;
        JS_ToInt32(ctx, &len, lenV);
        JS_FreeValue(ctx, lenV);
        for (int32_t i = 0; i < len; ++i) {
            JSValue it = JS_GetPropertyUint32(ctx, val, i);
            const char* s = JS_ToCString(ctx, it);
            if (s) { out.emplace_back(s); JS_FreeCString(ctx, s); }
            JS_FreeValue(ctx, it);
        }
        return true;
    }
    const char* s = JS_ToCString(ctx, val);
    if (!s) return false;
    out.emplace_back(s);
    JS_FreeCString(ctx, s);
    return true;
}

// 遍历所有参数追加到 out
JSValue ChainAppend(JSContext* ctx, JSValueConst this_val, int argc,
                    JSValueConst* argv, std::vector<std::string> ProjectState::*field) {
    JsProject* p = GetProject(ctx, this_val);
    if (!p) return JS_EXCEPTION;
    for (int i = 0; i < argc; ++i)
        AppendStrings(ctx, argv[i], p->state.*field);
    return JS_DupValue(ctx, this_val);  // 返回自身以支持链式
}

JSValue proj_type(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsProject* p = GetProject(ctx, this_val);
    if (!p) return JS_EXCEPTION;
    const char* s = JS_ToCString(ctx, argv[0]);
    if (s) { p->state.type = s; JS_FreeCString(ctx, s); }
    return JS_DupValue(ctx, this_val);
}
JSValue proj_name(JSContext* ctx, JSValueConst this_val, int, JSValueConst* argv) {
    JsProject* p = GetProject(ctx, this_val);
    if (!p) return JS_EXCEPTION;
    const char* s = JS_ToCString(ctx, argv[0]);
    if (s) { p->state.name = s; JS_FreeCString(ctx, s); }
    return JS_DupValue(ctx, this_val);
}
JSValue proj_sources(JSContext* ctx, JSValueConst t, int a, JSValueConst* v) {
    return ChainAppend(ctx, t, a, v, &ProjectState::sources);
}
JSValue proj_include(JSContext* ctx, JSValueConst t, int a, JSValueConst* v) {
    return ChainAppend(ctx, t, a, v, &ProjectState::include);
}
JSValue proj_define(JSContext* ctx, JSValueConst t, int a, JSValueConst* v) {
    return ChainAppend(ctx, t, a, v, &ProjectState::define);
}
JSValue proj_link(JSContext* ctx, JSValueConst t, int a, JSValueConst* v) {
    return ChainAppend(ctx, t, a, v, &ProjectState::link);
}
JSValue proj_linkFlags(JSContext* ctx, JSValueConst t, int a, JSValueConst* v) {
    return ChainAppend(ctx, t, a, v, &ProjectState::linkFlags);
}
JSValue proj_cxxFlags(JSContext* ctx, JSValueConst t, int a, JSValueConst* v) {
    return ChainAppend(ctx, t, a, v, &ProjectState::cxxFlags);
}

// cp.commit() —— 立即执行当前累计配置
JSValue proj_commit(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    JsProject* p = GetProject(ctx, this_val);
    if (!p) return JS_EXCEPTION;
    ProjectState& s = p->state;
    Engine& e = *g_engine;

    if (s.name.empty() && s.commitCount == 0) {
        // 未显式命名时，用第一个源文件名
        s.name = "a.out";
    }

    if (s.sources.empty()) {
        Log("[foxrci] commit: no sources");
        e.lastExit = 1;
        return JS_DupValue(ctx, this_val);
    }

    if (s.commitCount == 0) {
        DoCompileStep(e, s);
        s.commitCount++;
        // ninja 后端：build.ninja 已经描述完整的「编译 + 链接」流程，
        // 后续 commit 不再自己编/链，交给 ninja 执行。
        if (e.backend == "ninja") s.commitCount = 999;
    } else if (s.commitCount == 999) {
        // ninja 后端已经生成，直接忽略后续 commit
    } else {
        DoLinkStep(e, s);
        s.commitCount++;
    }
    return JS_DupValue(ctx, this_val);
}

void ProjFinalizer(JSRuntime*, JSValue val) {
    JsProject* p = static_cast<JsProject*>(JS_GetOpaque(val, g_project_class));
    delete p;
}

// ---------------------------------------------------------------------------
// QuickJS 绑定：ci
// ---------------------------------------------------------------------------

JSValue ci_create_project(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue obj = JS_NewObjectClass(ctx, g_project_class);
    if (JS_IsException(obj)) return obj;
    auto* jp = new JsProject();
    JS_SetOpaque(obj, jp);
    return obj;
}

JSValue ci_backend(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    const char* s = JS_ToCString(ctx, argv[0]);
    if (s) { g_engine->backend = s; JS_FreeCString(ctx, s); }
    return JS_UNDEFINED;
}

// 注册 Project 类与 ci 对象
bool RegisterBindings(Engine& e) {
    JSContext* ctx = e.ctx;

    g_project_class = 0;
    JS_NewClassID(&g_project_class);
    JSClassDef def{};
    def.class_name = "Project";
    def.finalizer = ProjFinalizer;
    if (JS_NewClass(e.rt, g_project_class, &def) < 0) return false;

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "name",      JS_NewCFunction(ctx, proj_name, "name", 1));
    JS_SetPropertyStr(ctx, proto, "type",      JS_NewCFunction(ctx, proj_type, "type", 1));
    JS_SetPropertyStr(ctx, proto, "sources",   JS_NewCFunction(ctx, proj_sources, "sources", 1));
    JS_SetPropertyStr(ctx, proto, "include",   JS_NewCFunction(ctx, proj_include, "include", 1));
    JS_SetPropertyStr(ctx, proto, "define",    JS_NewCFunction(ctx, proj_define, "define", 1));
    JS_SetPropertyStr(ctx, proto, "link",      JS_NewCFunction(ctx, proj_link, "link", 1));
    JS_SetPropertyStr(ctx, proto, "linkFlags", JS_NewCFunction(ctx, proj_linkFlags, "linkFlags", 1));
    JS_SetPropertyStr(ctx, proto, "cxxFlags",  JS_NewCFunction(ctx, proj_cxxFlags, "cxxFlags", 1));
    JS_SetPropertyStr(ctx, proto, "commit",    JS_NewCFunction(ctx, proj_commit, "commit", 0));
    JS_SetClassProto(ctx, g_project_class, proto);

    // ci 对象
    JSValue ci = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ci, "create_project",
                      JS_NewCFunction(ctx, ci_create_project, "create_project", 0));
    JS_SetPropertyStr(ctx, ci, "backend",
                      JS_NewCFunction(ctx, ci_backend, "backend", 1));

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "ci", ci);
    JS_FreeValue(ctx, global);
    return true;
}

// ---------------------------------------------------------------------------
// 主流程
// ---------------------------------------------------------------------------

int RunBuild(const std::string& root, const std::string& backend) {
    Engine e;
    e.root = root;
    e.backend = backend;
    e.cc = GetEnv("CC", "gcc");
    e.cxx = GetEnv("CXX", "g++");
    g_engine = &e;

    std::string script = ReadFileText(root + "/build.js");
    if (script.empty()) {
        Log("[foxrci] build.js not found or empty in " + root);
        return 2;
    }

    e.rt = JS_NewRuntime();
    e.ctx = JS_NewContext(e.rt);
    if (!RegisterBindings(e)) {
        Log("[foxrci] failed to register bindings");
        return 3;
    }

    // 执行脚本：定义 Main 类
    JSValue v = JS_Eval(e.ctx, script.c_str(), script.size(), "build.js",
                        JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JSValue exc = JS_GetException(e.ctx);
        const char* s = JS_ToCString(e.ctx, exc);
        Log(std::string("[foxrci] build.js error: ") + (s ? s : "?"));
        JS_FreeCString(e.ctx, s);
        JS_FreeValue(e.ctx, exc);
        return 4;
    }
    JS_FreeValue(e.ctx, v);

    // Main 是全局作用域的 class 声明，属于词法绑定，不能通过 globalThis 取出。
    // 所以在同一个全局上下文里再 eval 一段 `new Main(ci)`，把入口实例化。
    // 需要先确认 Main 存在，给一个友好的错误信息。
    int rc = 0;
    do {
        const char* probe = "typeof Main";
        JSValue t = JS_Eval(e.ctx, probe, __builtin_strlen(probe), "<probe>",
                            JS_EVAL_TYPE_GLOBAL);
        bool hasMain = false;
        if (!JS_IsException(t)) {
            const char* s = JS_ToCString(e.ctx, t);
            hasMain = s && std::strcmp(s, "function") == 0;
            JS_FreeCString(e.ctx, s);
        }
        JS_FreeValue(e.ctx, t);
        if (!hasMain) {
            Log("[foxrci] build.js must define `class Main`");
            rc = 5;
            break;
        }

        const char* run = "new Main(ci)";
        JSValue inst = JS_Eval(e.ctx, run, __builtin_strlen(run), "<run>",
                               JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(inst)) {
            JSValue exc = JS_GetException(e.ctx);
            const char* s = JS_ToCString(e.ctx, exc);
            Log(std::string("[foxrci] running Main failed: ") + (s ? s : "?"));
            JS_FreeCString(e.ctx, s);
            JS_FreeValue(e.ctx, exc);
            rc = 6;
            break;
        }
        JS_FreeValue(e.ctx, inst);
        rc = e.lastExit;
    } while (false);

    g_engine = nullptr;
    JS_FreeContext(e.ctx);
    JS_FreeRuntime(e.rt);
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = CurrentDir();
    std::string backend = GetEnv("FOXRCI_BACKEND", "gcc");

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-C" && i + 1 < argc) { root = argv[++i]; }
        else if (a == "--backend" && i + 1 < argc) { backend = argv[++i]; }
        else if (a == "--ninja") { backend = "ninja"; }
        else if (a == "--help" || a == "-h") {
            std::printf("usage: foxrci [-C dir] [--backend gcc|ninja] [--ninja]\n");
            return 0;
        }
    }

    return RunBuild(root, backend);
}
