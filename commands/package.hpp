#pragma once
#include <string>
#include <vector>

namespace Commands {

// 贯穿整个 foxrci 命令执行过程的数据包：
// 每条命令把名字压进 history_commands，出错时把信息写进 error。
struct CommandRetrunPackage {
    std::vector<std::string> history_commands;
    std::string error;
};

} // namespace Commands
