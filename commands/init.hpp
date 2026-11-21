#pragma once
#include "package.hpp"

namespace Commands {

// foxrci --init：向磁盘写入前置 JS 文件（console.js / File.js 等）。
// 目前这些文件内建到了 C++ 类里，所以暂时直接返回 true。
bool init(CommandRetrunPackage &pkg);

} // namespace Commands
