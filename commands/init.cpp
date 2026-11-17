#include "init.hpp"
#include <any>
#include <exception>
#include <fstream>
#include <filesystem>
#include <string>
#include <iostream>

namespace Commands
{

// 绑定单个文件到单个 ExtravtFileList 的一个节点
void addFile(std::string filename, std::string file_content, ExtractFileList& efl)
{
    efl.file[filename] = std::move(file_content);
}

// 绑定文件内容到 ExtractFileList
bool addExtractFile(CommandRetrunPackage& pkg)
{
    // 检查是否存在错误
    if (pkg.has_error == true) return false;
    // 向 pkg 里面传递数据
    pkg.history_commands.push_back("addExtractFile");
    // 创建 elf 对象
    try {
        ExtractFileList efl;
        addFile(
            "test.js",
            "console.log('Hello World')",
            efl
        );
        pkg.belt["addExtractFile"] = std::move(efl);
    } catch(std::exception e) {
        // 填写报错信息
        pkg.has_error = true;
        pkg.error = "has exception. wait to be more completly.";
        return false;
    }
    // 表示执行成功
    return true;
}

// 解压文件以及创建项目目录
bool init(CommandRetrunPackage& pkg)
{
    // 如果上一条命令执行失败，则返回false
    if (!addExtractFile(pkg))
    {
        std::cout << "at: " << pkg.history_commands.at(pkg.history_commands.size() - 1) << std::endl;
        std::cout << "err: " << pkg.error << std::endl;
        return false;
    }
    // 向 pkg 里面传递数据
    pkg.history_commands.push_back("init");

    // 给文件从 any 里面取出来，然后 extract
    auto& files = std::any_cast<ExtractFileList&>(pkg.belt.at("addExtractFile"));
    for (auto& [filename, file_content] : files.file)
    {
        // 获取当前目录
        std::filesystem::path current_dir = std::filesystem::current_path();
        // 路径拼接
        std::filesystem::path full_path = current_dir / filename;
        // 创建父目录
        std::filesystem::create_directories(full_path.parent_path());
        // 创建文件
        std::ofstream ofs(full_path);
        std::cout << "create: " << filename << std::endl;
        if (!ofs)
        {
            // 错误处理
            pkg.has_error = true;
            pkg.error = "ofstream error.";
            return false;
        }
        ofs << file_content;
        std::cout << filename << " has writen down" << std::endl;
    }

    // 表示执行成功
    return true;
}

}
