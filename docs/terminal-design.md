# 终端命令与输入交互依据

本轮扩展保留受限文件系统与 Qt 界面，命令始终由游戏内部执行。

## 参考资料

- [Bash 管道](https://www.gnu.org/software/bash/manual/html_node/Pipelines.html)：连接前一条命令的标准输出与后一条的标准输入，错误流保持独立；默认以最后一条命令的退出状态作为管道状态。
- [Bash 重定向](https://www.gnu.org/s/bash/manual/html_node/Redirections.html)：在执行命令前准备重定向，按出现顺序处理，`>` 截断，`>>` 追加，`<` 提供输入。
- [Readline 历史搜索](https://www.gnu.org/software/bash/manual/html_node/Searching.html)与[剪切、恢复](https://www.gnu.org/s/bash/manual/html_node/Readline-Killing-Commands.html)：参考 Ctrl+R、Ctrl+U/K/W/Y 和连续剪切的操作习惯。搜索框使用 Qt 输入控件，以支持中文输入法；Esc 接受后继续编辑，Ctrl+G 还原原输入。
- [GNU grep 手册](https://www.gnu.org/software/grep/manual/grep.html)：固定文字匹配明确要求 `-F`，行号从 1 开始，多个文件与递归搜索带文件名，使用 0/1/2 退出状态区分匹配、无匹配与错误。
- [GNU find 起点](https://www.gnu.org/software/findutils/manual/html_node/find_html/Starting-points.html)和[名称模式](https://www.gnu.org/software/findutils/manual/html_node/find_html/Shell-Pattern-Matching.html)：目录树遍历、文件名模式、隐藏项和相对起点的输出形式。当前只实现一个起点以及名称、类型和最大深度筛选。
- [GNU head](https://www.gnu.org/software/coreutils/manual/html_node/head-invocation.html)和[tail](https://www.gnu.org/software/coreutils/manual/html_node/tail-invocation.html)：默认前后 10 行、`-n`、标准输入和多文件标题。当前只提供非负行数的 UTF-8 文本截取。
- [Vim 重新编辑与保存](https://vimhelp.org/editing.txt.html)：参考 `:e` / `:e!` 重新读取、保护未保存缓冲区，以及明确强制写入的命令习惯。
- [less 项目](https://greenwoodsoftware.com/less/)及[作者维护的源码和手册](https://github.com/gwsw/less)：参考分页、/ 与 ? 搜索、n/N 重复搜索、q 退出的使用方式。
- [util-linux login 手册](https://man7.org/linux/man-pages/man1/login.1.html)：参考控制台用户名、隐藏密码输入、验证后进入 Shell、显示上次成功登录时间的流程。
- [Qt QPasswordDigestor](https://doc.qt.io/qt-6/qpassworddigestor.html)与 [QLineEdit EchoMode](https://doc.qt.io/qt-6/qlineedit.html#EchoMode-enum)：使用 Qt 的 PBKDF2 实现和 `NoEcho` 输入，不自行实现密码派生算法。

## 当前范围

命令执行先解析完整管道并验证支持范围，再逐条执行。标准输出是保留换行的 UTF-8 文本；标准错误单独累积，供界面显示。内部命令同步处理已载入的文本，不创建宿主进程，也不模拟进程调度。GUI 命令不进入管道，修改目录或文件的命令只单独执行，避免产生虚假的子 Shell 状态变化。每条命令最多一个输入和一个输出重定向；不提供文件描述符、脚本、变量展开或作业控制。

命令历史与文件世界分开，位于 world 同级的 history.json，使用原子写入，最多 1000 条和 4 MiB，损坏的历史不自动覆盖。补全复用命令的词法规则，按光标位置识别命令、路径、管道与重定向，并对候选路径重新引用或转义。

分页器保留阅读前的终端记录，内容只读，使用 Qt 正则表达式搜索，不实现完整 less 的选项、文件切换、外部命令或跟随文件增长。完整操作和限制见根目录 README。所有自动检查使用隔离的临时世界及设置目录。

## 文件工具与外部编辑

`find` 每次遍历磁盘上的目录，按名称、类型和深度筛选，使用显式栈保持父项先于子项的输出顺序。它与其他文件命令共享路径验证，不遍历链接，也不把世界文件替换为内存缓存。`head` / `tail` 对文件或输入流按 LF 边界截取，保留 CRLF 和未换行的末行，标准输出与诊断继续分开。

终端启动 Vim 或从 mc 的 F4 进入时，重新读取当前磁盘内容，记录路径、是否存在和 SHA-256 内容摘要。普通保存前读取文件的实际字节并比较摘要，拒绝覆盖外部修改、外部删除或后来创建的同名目标；成功保存更新基线。未改变字节内容的时间戳变更不视为冲突。该机制是保存前的冲突检查和原子写入，不是宿主文件锁，不能提供跨进程的原子比较并写入保证。

`:e` 在缓冲区未修改时重新读取；`:e!` 明确丢弃本地修改并重新读取，成功后清空旧撤销历史。读取失败、非法 UTF-8 或原文件已删除时保留缓冲区。`:w!` 和 `:wq!` 明确覆盖或重建目标，但仍检查世界目录、文件类型和路径边界，不绕过这些限制。不实现自动跟随、文件监听、合并、备份和跨进程锁。

## 本地登录

入口由 `SessionWidget` 管理：启动显示 `LoginWidget`，验证成功后才创建 `TerminalWidget` 和文件世界，退出登录时销毁当前终端并重新显示登录页。登录凭据不经过命令解析和历史记录。共享的 `LocalIdentity` 供登录、提示符、会话栏和 `whoami` 使用，目前只支持固定的 `guest@silly`。

首次设置由两次隐藏密码输入确认；后续输入用户名和密码。`QLineEdit::NoEcho` 不显示密码字符或长度，后台任务使用 `QPasswordDigestor` 的 PBKDF2-HMAC-SHA256、600000 次迭代和 32 字节随机盐生成 32 字节校验值。错误用户名和密码均显示 `Login incorrect`，短暂等待由事件循环完成；文件错误单独报告。`account.json` 使用受限文件权限和原子写入，写入前加锁并重读，防止重复设置或并发丢失登录时间；损坏数据保留，不自动重建或允许进入终端。

`exit` 和 `logout` 当前只支持无参数调用，不进入管道或接受重定向。重新登录保留既有文件、历史、浏览器状态和保存的当前目录。这里选择保留当前目录以衔接现有持久化行为；没有模拟完整 Unix 登录环境、PAM、进程、远程认证、多用户或文件权限。
