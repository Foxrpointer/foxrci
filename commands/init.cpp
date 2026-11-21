#include "init.hpp"
#include "../parser/module_loader.hpp"

namespace Commands {

bool init(CommandRetrunPackage &pkg) {
    pkg.history_commands.push_back("--init");
    // 调各动态库模块的 extract：向磁盘写入前置 js 文件。
    // 目前没有这些文件，各模块直接 return true。
    return module_loader::extract_all(pkg);
}

} // namespace Commands
