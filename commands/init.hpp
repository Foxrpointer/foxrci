#ifndef COMMANDS_INIT_HPP
#define COMMANDS_INIT_HPP

#include <vector>
#include <string>
#include <any>
#include <map>

namespace Commands
{
// --init：需要解压的文件列表
struct ExtractFileList
{
    // filename : file_content
    std::map<std::string, std::string> file;
};

// 作用于命令之间链式调用的参数传递
// PS: 链式调用指
// $> foxrci --init build => foxrci --init | foxrci build
struct CommandRetrunPackage
{
    using Conveyor = std::map<std::string, std::any>;

    // 已经执行过的命令
    std::vector<std::string> history_commands;
    // 上一个命令的报错内容，以及是否出现错误
    bool has_error = false;
    std::string error;
    // 上一些命令留下的东西
    Conveyor belt;
};

bool init(CommandRetrunPackage& pkg);

}
#endif
