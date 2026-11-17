#include "commands/init.hpp"
#include "commands/run_js.hpp"
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
    Commands::js_runtime_init();

    // 命令
    for (std::string& arg : args)
    {
        if (arg == "--init")
        {
            if (!Commands::init(pkg))
            {
                std::cout << "at: " << pkg.history_commands.at(pkg.history_commands.size() - 1) << std::endl;
                std::cout << "err: " << pkg.error << std::endl;
                continue;
            }
        }

        // 这个AI真的是瞎写 --kyuujuu 我改了一下
        // 直接执行 JS 文件，复用共享上下文
        if (!Commands::run_js_file(arg + ".js", pkg))
        {
            std::cout << "at: " << pkg.history_commands.at(pkg.history_commands.size() - 1) << std::endl;
            std::cout << "err: " << pkg.error << std::endl;
        }
    }

    // 清理 JS 运行时
    Commands::js_runtime_cleanup();
}
