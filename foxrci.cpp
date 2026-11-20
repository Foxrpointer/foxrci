#include "init.hpp"
#include "parser/parser.hpp"
#include "vector"
#include <iostream>

std::vector<std::string> c_args_to_cpp_strings(char** argv)
{
    std::vector<std::string> result;
    if (argv == nullptr) return result;
    for (char** p = argv; *p != nullptr; ++p)
    {
        result.emplace_back(*p);
    }
    return result;
}

// 取最近一条命令名，防止 history_commands 为空时越界
static const std::string& last_command(const Commands::CommandRetrunPackage& pkg)
{
    static const std::string unknown = "<unknown>";
    if (pkg.history_commands.empty()) return unknown;
    return pkg.history_commands.back();
}

int main(int argc, char** argv)
{
    std::vector<std::string> args = c_args_to_cpp_strings(argv);
    args.erase(args.begin());   // 擦除第一个 arg，因为第一个是程序本身

    // 默认执行$> foxrci build，若只有程序本身一个参数的话
    if (args.size() == 0) args.push_back("build");

    // 创建 CommandReturnPackage
    Commands::CommandRetrunPackage pkg;

    // 初始化 JS 运行时（整个程序共享一个上下文）
    // 在这个里面还有全局函数的注册
    Parser* parser = new Parser();

    // 命令
    for (std::string& arg : args)
    {
        if (arg == "--init")
        {
            if (!Commands::init(pkg))
            {
                std::cout << "at: " << last_command(pkg) << std::endl;
                std::cout << "err: " << pkg.error << std::endl;
                continue;
            }
        }

        // 这个AI真的是瞎写 --kyuujuu 我改了一下
        // 直接执行 JS 文件，复用共享上下文
        if (!parser->run_js_file(arg + ".js", pkg))
        {
            std::cout << "at: " << last_command(pkg) << std::endl;
            std::cout << "err: " << pkg.error << std::endl;
        }
    }

    // 清理 JS 运行时
    delete parser;
}
