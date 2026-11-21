#include "module_loader.hpp"
#include "../module_abi.hpp"
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#endif

namespace {

struct LoadedModule {
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void *handle = nullptr;
#endif
    foxrci_module_extract_fn extract = nullptr;
    foxrci_module_register_fn reg = nullptr;
    std::string path;
};

std::vector<LoadedModule> &modules() {
    static std::vector<LoadedModule> mods;
    return mods;
}

// 遍历 dir 下所有动态库文件（Windows: *.dll，POSIX: *.so），加载并取出两个入口。
bool load_modules_from(const std::string &dir, Commands::CommandRetrunPackage &pkg) {
    bool all_ok = true;
#ifdef _WIN32
    std::string pattern = dir + "\\*.dll";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;   // 目录不存在/为空：不算错误
    do {
        std::string path = dir + "\\" + fd.cFileName;
        HMODULE m = LoadLibraryA(path.c_str());
        if (!m) {
            pkg.error = "LoadLibrary failed: " + path;
            all_ok = false;
            continue;
        }
        auto extract = reinterpret_cast<foxrci_module_extract_fn>(
            reinterpret_cast<void *>(GetProcAddress(m, "foxrci_module_extract")));
        auto reg = reinterpret_cast<foxrci_module_register_fn>(
            reinterpret_cast<void *>(GetProcAddress(m, "foxrci_module_register")));
        if (!reg) {
            pkg.error = "missing foxrci_module_register: " + path;
            FreeLibrary(m);
            all_ok = false;
            continue;
        }
        modules().push_back({m, extract, reg, path});
    } while (FindNextFileA(h, &fd));
    CloseHandle(h);
#else
    DIR *d = opendir(dir.c_str());
    if (!d) return true;
    while (dirent *e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 3 || name.substr(name.size() - 3) != ".so") continue;
        std::string path = dir + "/" + name;
        void *m = dlopen(path.c_str(), RTLD_NOW);
        if (!m) {
            pkg.error = std::string("dlopen failed: ") + path + ": " + dlerror();
            all_ok = false;
            continue;
        }
        auto extract = reinterpret_cast<foxrci_module_extract_fn>(
            dlsym(m, "foxrci_module_extract"));
        auto reg = reinterpret_cast<foxrci_module_register_fn>(
            dlsym(m, "foxrci_module_register"));
        if (!reg) {
            pkg.error = "missing foxrci_module_register: " + path;
            dlclose(m);
            all_ok = false;
            continue;
        }
        modules().push_back({m, extract, reg, path});
    }
    closedir(d);
#endif
    return all_ok;
}

} // namespace

namespace module_loader {

bool load_all(const std::string &dir, Commands::CommandRetrunPackage &pkg) {
    pkg.history_commands.push_back("module_loader:load_all(" + dir + ")");
    return load_modules_from(dir, pkg);
}

// 依次调用各模块的 extract（foxrci --init 时执行）。
bool extract_all(Commands::CommandRetrunPackage &pkg) {
    pkg.history_commands.push_back("module_loader:extract_all");
    bool ok = true;
    for (auto &m : modules()) {
        if (m.extract && !m.extract(pkg)) ok = false;
    }
    return ok;
}

// 依次调用各模块的 register，向全局函数/类注册。
bool register_all(JSContext *ctx, Commands::CommandRetrunPackage &pkg) {
    pkg.history_commands.push_back("module_loader:register_all");
    bool ok = true;
    for (auto &m : modules()) {
        if (!m.reg(ctx, pkg)) ok = false;
    }
    return ok;
}

} // namespace module_loader
