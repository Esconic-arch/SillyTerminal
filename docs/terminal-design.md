# 终端命令与输入交互依据

本轮扩展保留受限文件系统与 Qt 界面，命令始终由游戏内部执行。

## 参考资料

- [Bash 管道](https://www.gnu.org/software/bash/manual/html_node/Pipelines.html)：连接前一条命令的标准输出与后一条的标准输入，错误流保持独立；默认以最后一条命令的退出状态作为管道状态。
- [Bash 重定向](https://www.gnu.org/s/bash/manual/html_node/Redirections.html)：在执行命令前准备重定向，按出现顺序处理，`>` 截断，`>>` 追加，`<` 提供输入。
- [Readline 历史搜索](https://www.gnu.org/software/bash/manual/html_node/Searching.html)与[剪切、恢复](https://www.gnu.org/s/bash/manual/html_node/Readline-Killing-Commands.html)：参考 Ctrl+R、Ctrl+U/K/W/Y 和连续剪切的操作习惯。搜索框使用 Qt 输入控件，以支持中文输入法；Esc 接受后继续编辑，Ctrl+G 还原原输入。
- [GNU grep 手册](https://www.gnu.org/software/grep/manual/grep.html)：固定文字匹配明确要求 `-F`，行号从 1 开始，多个文件与递归搜索带文件名，使用 0/1/2 退出状态区分匹配、无匹配与错误。
- [less 项目](https://greenwoodsoftware.com/less/)及[作者维护的源码和手册](https://github.com/gwsw/less)：参考分页、/ 与 ? 搜索、n/N 重复搜索、q 退出的使用方式。

## 当前范围

命令执行先解析完整管道并验证支持范围，再逐条执行。标准输出是保留换行的 UTF-8 文本；标准错误单独累积，供界面显示。内部命令同步处理已载入的文本，不创建宿主进程，也不模拟进程调度。GUI 命令不进入管道，修改目录或文件的命令只单独执行，避免产生虚假的子 Shell 状态变化。每条命令最多一个输入和一个输出重定向；不提供文件描述符、脚本、变量展开或作业控制。

命令历史与文件世界分开，位于 world 同级的 history.json，使用原子写入，最多 1000 条和 4 MiB，损坏的历史不自动覆盖。补全复用命令的词法规则，按光标位置识别命令、路径、管道与重定向，并对候选路径重新引用或转义。

分页器保留阅读前的终端记录，内容只读，使用 Qt 正则表达式搜索，不实现完整 less 的选项、文件切换、外部命令或跟随文件增长。完整操作和限制见根目录 README。所有自动检查使用隔离的临时世界及设置目录。
