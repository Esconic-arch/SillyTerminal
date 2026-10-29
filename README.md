# SillyTerminal

使用 **C++17、Qt 6 Widgets、CMake** 开发的伪终端初版。在 Windows 和 macOS 上使用同一套源码，分别编译运行。

整体目标是一台持续存在、能够探索和操作预设虚构系统的“黑客电脑”。已确认目标与后续讨论方向见 [项目方向](docs/project-vision.md)。

多对话开发的目录隔离、任务分工和合并流程见 [并行开发指南](docs/parallel-development.md)。

新对话可直接读取 [角色说明](docs/roles/README.md) 中对应的职责文档，先了解项目和确认分工，之后再按需讨论或开发。

界面采用接近 macOS Terminal 的简洁深色风格：石墨灰背景、轻量会话栏、柔和的路径强调色和等宽字体。保留系统原生窗口控制，不绘制仿制的红黄绿按钮。会话栏和窗口标题会显示当前目录；原来的底部快捷键栏已移除，按 `F1` 或输入 `help` 查看操作说明。

默认行距为 **115%**。点击右上角 **Aa**、右键选择 **Font & spacing...**，或按 **Ctrl+,**（macOS 可用 **Command+,**），可以选择已安装的等宽字体、字号（8–32 pt）和行距（100%–160%）。设置会实时预览，点击 **OK** 后保存到本机用户偏好；**Cancel** 恢复修改前的外观，**Restore Defaults** 恢复默认值。修改外观不会清空历史输出或正在输入的命令。要使用新字体，先在系统中安装字体，再重新启动程序。

## 直接运行（当前 Windows 电脑）

第一次构建完成后，双击：

```text
dist\SillyTerminal\SillyTerminal.exe
```

也可以在项目根目录的 PowerShell 中运行：

```powershell
.\dist\SillyTerminal\SillyTerminal.exe
```

分发时需要复制整个 `dist\SillyTerminal` 文件夹，保留里面的 DLL 和 `platforms` 等子目录。玩家不需要安装 Qt 或编译器。

### 制作 Windows 试玩包

在项目根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -QtRoot D:\Qt\6.10.2\mingw_64 -MinGwRoot D:\Qt\Tools\mingw1310_64 -Package
```

脚本完成构建和全部测试后，使用 `windeployqt` 将程序和运行依赖部署到新的干净目录，加入试玩说明，生成 `dist\SillyTerminal-Windows-x64-日期时间.zip` 和 SHA256 校验文件。它会重新解压 ZIP，并在移除开发套件环境变量后检查启动、界面和 TLS 后端。该检查使用独立临时账户与设置，不调用真实 AI 服务。

直接把 ZIP 发给试玩者即可。对方完整解压到可写文件夹，打开其中的 `SillyTerminal`，双击 `SillyTerminal.exe`，首次设置自己的游戏内密码。不要只发 EXE，也不要将整个项目或自己使用过的 `runtime/` 一起压缩；脚本从构建后的 EXE 重新部署，不复制这些个人数据或宿主 AI 设置。包内附有 [试玩说明](docs/windows-playtest.txt)。

Windows ZIP 供 64 位 Windows 使用；Mac 需要在 macOS 上单独构建并用 `macdeployqt` 部署 `.app`，不能运行这个 EXE。部署工具的行为参考 [Qt Windows 部署](https://doc.qt.io/qt-6/windows-deployment.html)和 [Qt macOS 部署](https://doc.qt.io/qt-6/macos-deployment.html)。

## 登录与退出

每次打开程序先进入文字控制台登录页。当前只有本地账户 **`guest`**，主机名为 **`silly`**。

首次运行会显示 `Local account setup`：在 `New password:` 后输入密码并按回车，再在 `Retype new password:` 后重复一次。两次一致后进入终端。密码不能为空，至多 1024 个字符；支持中文，首尾空格也是密码的一部分。输入密码时不显示字符或星号。

以后启动时，在 `silly login:` 后输入 `guest`，按回车，再输入自己设置的密码并回车。错误时显示 `Login incorrect`，稍候可以重试；`Ctrl+C` 可以取消当前登录输入并回到起点。账户文件读写失败时显示具体错误，不会自动覆盖账户或绕过登录。

登录后显示实际记录的上次成功登录时间，首次登录省略这一行。输入 `exit` 或 `logout` 返回登录页；关闭窗口退出程序。文件、当前目录、命令历史和浏览器状态持续保留，退出登录只结束当前终端会话。字体设置在登录后通过 **Aa** 调整，再次进入登录页也沿用保存的字体与行距。

账户保存在 `runtime/account.json`，采用随机盐和 PBKDF2-HMAC-SHA256（600000 次）保存密码校验数据，不保存明文密码，密码也不进入终端输出或命令历史。当前不提供多账户、权限控制或终端内修改密码。忘记密码时可关闭程序，将 `runtime/account.json` 改名为备份文件；下次启动重新设置密码，保留其余运行数据。这是游戏中的本地账户，与宿主系统账户独立。

## 怎么操作

### AI 服务配置

点击窗口右上角 **AI**，可以配置 DeepSeek 的 API 根地址、密钥和模型，以及自托管 ComfyUI 的服务器地址和可选 Bearer token。登录前也能打开设置。

DeepSeek 默认地址为 `https://api.deepseek.com`；**Test connection / load models** 检查 API 访问并加载模型列表。ComfyUI 默认地址为 `http://127.0.0.1:8188`；**Test connection** 检查服务器是否返回系统信息。这些检查不生成文字或图片。检查当前草稿后，点击 **Save** 保存，**Cancel** 放弃修改。

服务配置属于宿主应用的用户设置。密钥使用 Windows Credential Manager 或 macOS Keychain 保存，和游戏内账户、文件与命令历史分开。摄像头已接入DeepSeek → ComfyUI生成适配器；还需在 **Camera generation** 中选择与本机ComfyUI匹配的工作流配置。可导出示例，替换checkpoint与API工作流并填写输入绑定。说明见[AI服务配置](docs/ai-service-configuration.md)和[摄像头生成配置](docs/ai-camera-generation.md)。

**Text preset** 提供可选的文本预设入口：暂时留空即可使用内置指令，也可导出空模板，之后填入模块、启用并选择文件。支持本地校验和消息预览；拍摄会保存当时的预设与完整消息，修改源文件供新拍摄使用。原生格式和后续酒馆导入边界见[文本预设接口](docs/ai-prompt-presets.md)。

### 固定家庭摄像头

输入 `camera 192.168.50.23` 打开快照客户端，也可以输入 `camera` 后在窗口中填写地址。初始设备为客厅的 **Hearth H1 Fixed Indoor**，与虚构的书桌电脑位于同一家庭局域网。使用 `viewer` / `teacup-27` 登录后，可以刷新设备信息并点击 **Take snapshot** 请求一张照片；地址和登录不会访问宿主网络或真实摄像头。密码在窗口内输入，不进入命令历史。新世界的 `~/documents/home-network.txt` 保存了安装记录；已有世界保持原样，不补写新模板文件。

摄像头固定视角，无转向或实时视频。打开客户端、登录、查看历史都不触发新的拍照。界面提供拍摄时刻、任务状态、重试、图片预览与 **Save image...**；保存目的地是虚构电脑中的真实文件路径，已有文件不会被覆盖。普通用户 `monitor` / `status-only` 仅能查看设备状态，不能拍照。

点击拍照会保存冻结的场景和生成配置，异步调用DeepSeek整理提示词，再提交ComfyUI工作流。完成后图片保存在真实运行文件中并显示；关闭拍照窗口不取消后台任务。缺少密钥、生成配置或工作流不兼容时明确报错，不以演示图片替代。仅连接检查成功还不能证明实际工作流已可用；真实模型的画面效果仍待配置后的样本验证。步骤及恢复范围见[摄像头生成配置](docs/ai-camera-generation.md)。

首次打开客户端时建立 `runtime/network/`，包含可用系统编辑器修改的 `device.json`、`scene.json` 和独立的 `captures/<拍摄标识>.json`。设备状态在连接、刷新、拍照和重试时重新读取；场景在每次新拍照时读取。可修改 `scene.json` 中的 `state.door`（`open` / `closed`）、`state.lamp`（`on` / `off`）、人物和物品等，再拍照观察记录变化。场景中的 `world_time` 是显式保存的世界时间；当前没有自动时钟或人物调度。

修改或删除源文件不会被模板自动修复；内容无效时显示错误并保留原文件。旧拍摄记录保留完整快照，重试使用原场景；已完成图片再次打开直接读取资源。设备地址、账户、在线、服务开关和隐私模式也由文件控制。详细结构、外部编辑规则和 AI 交接入口见 [摄像头实现说明](docs/camera-implementation.md)。

### 终端命令

在提示符后输入命令，按回车执行。当前目录初始为 `/home/guest`，显示为 `~`。

```text
guest@silly:~$ ls
guest@silly:~$ cat notes.txt
guest@silly:~$ cd documents
guest@silly:~/documents$ cat welcome.txt
guest@silly:~/documents$ cat "quick start.txt"
guest@silly:~/documents$ cd ..
guest@silly:~$ ls -a
guest@silly:~$ cat .config/session.conf
guest@silly:~$ cat /var/log/boot.log
```

| 命令 | 用途 |
| --- | --- |
| `help` | 查看命令说明 |
| `ls [-a] [路径]` | 列出文件；`-a` 包含隐藏项 |
| `cd [路径]` | 切换目录；不带参数返回主目录 |
| `cd ..` / `cd ~` / `cd -` | 上级目录 / 主目录 / 上一次的目录 |
| `pwd` | 显示完整路径 |
| `cat [文件 ...]` | 读取文本文件；省略文件时读取管道或 `<` 输入；带空格的路径使用引号 |
| `find [路径] [-name 模式] [-type f或d] [-maxdepth 层数]` | 按名称、类型和深度查找；`-iname` 忽略名称大小写 |
| `head [-n 行数] [文件 ...]` / `tail [-n 行数] [文件 ...]` | 查看开头 / 末尾的若干行，默认 10 行；也可读取管道或 `<` 输入 |
| `grep -F[nirv] <文字> [文件 ...]` | 搜索普通文字；`-n` 显示行号，`-i` 忽略大小写，`-r` 递归，`-v` 选择不匹配的行 |
| `less [文件]` | 分页阅读文本，也可读取管道或 `<` 输入 |
| `history [数量]` / `history -c` | 查看保存的命令历史 / 清空历史 |
| `mkdir [-p] <目录>` | 创建目录；`-p` 连同父目录一起创建 |
| `touch <文件>` | 创建空文件，或更新时间；不清空已有内容 |
| `vim <文件>` | 在终端区域中使用简易 Vim 编辑文本 |
| `browser [地址]` | 打开浏览器，省略地址进入收藏页 |
| `camera [地址]` | 打开固定摄像头快照客户端 |
| `mc [目录 [另一侧目录]]` | 打开双栏文件管理器 |
| `feh [文件或目录 ...]` | 查看本地 PNG/JPEG 图片；省略参数浏览当前目录 |
| `mv <源> <目标>` | 移动或重命名文件、目录 |
| `cp [-r] <源> <目标>` | 复制文件；`-r` 复制目录及其内容 |
| `rm [-r] <路径>` | 删除文件；`-r` 删除目录及其内容 |
| `echo [文字]` | 输出文字，也可以输入中文 |
| `echo 文字 > 文件` | 覆盖写入一行 UTF-8 文字 |
| `echo 文字 >> 文件` | 在文件末尾追加一行文字 |
| `whoami` | 查看当前用户名 |
| `exit` / `logout` | 退出当前登录会话，返回登录页 |
| `clear` | 清空屏幕，保留当前位置和历史命令 |

文件操作示例：

```text
cd ~
mkdir journals
touch journals/todo.txt
echo 第一条记录 > journals/todo.txt
echo 今天继续探索 >> journals/todo.txt
vim journals/todo.txt
cat journals/todo.txt
cp journals/todo.txt journals/backup.txt
mv journals/backup.txt journals/old.txt
rm journals/old.txt
```

文件编辑统一使用 `vim`：打开后按 `i` 输入，按 `Esc` 返回普通模式，再输入 `:wq` 保存并退出，或 `:q!` 放弃本次修改。新文件只在保存时创建。Vim 和 `cat` 读取 UTF-8 文本，不会把二进制或其他编码的文件自动转换后覆盖。

### 搜索、管道与重定向

`grep` 当前实现固定文字匹配模式，需要明确写出 `-F`，支持中文和组合选项。例如：

```sh
grep -Fn "南桥站" notes.txt
grep -rFin "warning" ~/documents
cat /var/log/boot.log | grep -F "warning"
grep -rFn "南桥站" ~/documents | less
ls ~/documents > file-list.txt
grep -Fn "南桥站" notes.txt > matches.txt
grep -Fn "warning" < /var/log/boot.log >> matches.txt
```

`|` 把前一条命令的标准输出送到后一条，支持连续多级管道。`>` 在命令执行前创建或清空目标文件，`>>` 追加且不会自动补入分隔换行，`<` 从文件提供输入。错误信息始终显示在终端，不进入管道或结果文件。与常用 Shell 一样，`cat file > file` 会先清空文件，读取失败的命令也可能留下空的重定向文件。`ls` 在管道和重定向中每行输出一个名称。

首版管道支持 `cat`、`echo`、`find`、`grep`、`head`、`help`、`history`、`less`、`ls`、`pwd`、`tail` 和 `whoami`。每条命令最多一个输入和一个输出重定向；交互式 `browser`、`camera`、`vim`、`mc`、`feh`、`clear` 及会话命令 `exit`、`logout` 不接受重定向，改变文件或当前目录的命令不放入管道。暂不支持 `&&`、`||`、`;`、后台任务、文件描述符重定向、Shell 通配符展开或变量展开。引号或反斜杠保护的 `|`、`<`、`>` 是普通文字。

`grep` 只处理 UTF-8 文本，`-r` 会搜索隐藏文件并跳过符号链接和目录联接。没有匹配时不显示错误；文件读取失败时会显示具体原因。它保留真实工具的退出状态约定：匹配为 0，无匹配为 1，读取或参数错误为 2。当前终端不提供 `$?` 展开。

### 文件查找和节选阅读

```sh
find ~/documents -type f -name "*.txt"
find . -maxdepth 2 -iname "*note*"
head -n 5 notes.txt
tail -n 20 /var/log/boot.log
find ~/documents -type f | grep -F ".txt" | head -n 10
cat /var/log/boot.log | tail -n 5 > recent-log.txt
```

`find` 省略起点时从当前目录开始，保留相对起点的输出形式，例如 `./documents/welcome.txt`。名称匹配仅针对文件名，支持 `*`、`?` 和方括号模式；`-name` 区分大小写，`-iname` 不区分。多个筛选条件同时满足才输出，`-type f` 只找文件，`-type d` 只找目录，`-maxdepth 0` 只检查起点本身。默认输出匹配路径，包含隐藏项，跳过符号链接和目录联接。当前一次指定一个起点，不支持 `-exec`、`-delete`、逻辑表达式或其他动作。Shell 本身不展开通配符，建议仍给模式加引号，以保持常用命令写法。

`head` / `tail` 默认取前 / 后 10 行；`-n 5` 和 `-n5` 都可用，行数为非负整数，0 不输出正文。支持多个文件及表示标准输入的 `-`，多文件输出带文件名标题，`--` 可用于分隔以连字符开头的文件名。文本保留原有 CRLF、中文和末行是否换行，不自动补换行。每次命令重新读取真实文件，首版只处理 UTF-8 文本，不提供按字节截取、负行数或 `tail -f` 持续跟随。

### 分页阅读

`less notes.txt` 或 `命令 | less` 会占用终端区域，退出后恢复终端记录。支持方向键或 `j` / `k` 逐行滚动，`Space` / `f` / `PageDown` 向后翻页，`b` / `PageUp` 向前翻页，`g` / `G` 到开头 / 末尾。`/` 向后搜索，`?` 向前搜索，输入后按回车；`n` / `N` 沿原方向 / 反方向继续搜索，到文件边界后提示未找到，不循环。搜索使用 Qt 的正则表达式语法，非法表达式会保留输入供修改。搜索框内 `Esc` 取消搜索；阅读时 `q` 或 `Esc` 退出。

分页器只读，支持鼠标选择和复制，使用终端的字体与行距；首版把 UTF-8 内容完整载入内存，不跟随文件新增内容，也不支持多个文件切换或运行外部命令。重定向的 `less` 直接输出文本，不打开阅读界面。

### 简易 Vim

输入 `vim notes.txt`（带空格的文件名使用引号），编辑器会占用当前终端区域。底部显示模式、文件路径和光标位置，`[+]` 表示有尚未保存的修改。退出后恢复之前的终端记录和当前位置。

最基本的使用流程：

1. 输入 `vim diary.txt` 并回车。
2. 按 `i` 进入 **INSERT** 模式，输入文字，可以换行、粘贴多行内容或输入中文。
3. 按 `Esc` 返回 **NORMAL** 模式。
4. 输入 `:wq` 并回车，保存并退出。之后可用 `cat diary.txt` 查看。

| 模式 | 按键或命令 | 作用 |
| --- | --- | --- |
| 普通 | `i` / `a` | 在光标前 / 后开始输入 |
| 普通 | `I` / `A` | 在本行首个非空白字符前 / 行尾开始输入 |
| 普通 | `o` / `O` | 在当前行下方 / 上方新建一行并输入 |
| 普通 | `h j k l` 或方向键 | 左、下、上、右移动 |
| 普通 | `0` / `$` | 跳到行首 / 最后一个字符 |
| 普通 | `gg` / `G` | 跳到文件开头 / 末尾 |
| 普通 | `w` / `b` | 移动到下一个 / 上一个单词 |
| 普通 | `x` / `dd` | 删除当前字符 / 当前行 |
| 普通 | `yy` / `p` | 复制当前行 / 粘贴最近复制或删除的内容 |
| 普通 | `u` / `Ctrl+R` | 撤销 / 重做 |
| 普通 | `/文字` 然后回车 | 搜索文字，搜索到末尾后会回到开头 |
| 普通 | `n` / `N` | 查找下一个 / 上一个匹配 |
| 插入 | `Esc` | 返回普通模式 |
| 命令行 | `:w` | 保存，继续编辑 |
| 命令行 | `:w!` / `:wq!` | 明确覆盖外部修改后保存 / 保存并退出 |
| 命令行 | `:e` / `:e!` | 重新读取当前文件 / 放弃本地修改后重新读取 |
| 命令行 | `:q` | 退出；有未保存修改时会提示并停留 |
| 命令行 | `:wq` | 保存并退出；保存失败时停留在编辑器 |
| 命令行 | `:q!` | 放弃未保存修改并退出 |
| 命令行 | `:x` | 有修改时保存，然后退出 |
| 命令行 | `:help` / `F1` | 查看简短按键提示 |

命令行或搜索输入中也可以按 `Esc` 取消。新文件在 `:w` 或 `:wq` 保存后才会创建；编辑内容仍保存在 `runtime/world/` 中。

Vim 打开文件时记录磁盘内容，普通保存前比较实际字节内容。如果系统编辑器改动了文件、文件被删除，或新文件的目标路径已被其他工具创建，保存会停留在编辑器中，保留双方内容并给出提示。可以用 `:e!` 放弃当前缓冲区修改并重新读取磁盘，或明确用 `:w!` / `:wq!` 覆盖；没有本地修改时也可用 `:e` 重新读取。读取失败、内容无效或原文件已删除时，重新读取会保留当前缓冲区；被删除的文件只有明确强制保存后才重建。mc 的 F4 使用同一套行为。强制保存仍受世界目录边界限制。

这是模拟的 Vim 常用功能子集，不是系统 Vim：暂不支持数字次数（例如 `3dd`）、可视模式、正则搜索、替换、宏、插件或另存为。搜索按普通文字匹配；`w` / `b` 使用 Qt 的单词边界。编辑窗口会把换行显示为统一的换行符，明确保存时以 UTF-8、LF 换行写入。

### 双栏文件管理器

输入 `mc`，或用 `mc ~/documents /var/log` 指定两侧目录。Tab 切换面板，方向键选择，Enter 进入目录或查看文本，Backspace 返回父目录，Insert 标记多项。F3 查看、F4 用已有 Vim 编辑，退出阅读或编辑后回到 mc；F5 复制、F6 移动或重命名、F7 建目录、F8 确认删除，F10 退出回到终端。也可以点击底部功能键，或用 Alt+数字代替 F1–F10（Alt+0 为 F10）。F1 查看软件帮助。

复制、移动默认指向另一侧目录，可修改目标路径；相对路径以活动面板为准。支持目录递归及标记多选，遇到已有目标会报错并保留原文件，不覆盖或合并。批量操作遇错停止并显示已完成数量。删除确认默认选择取消。Ctrl+R 刷新，Alt+C 切换目录，Alt+. 显示或隐藏点文件。文件修改立即保存；退出不改变终端目录，另一侧目录保存在虚构电脑的 `~/.config/mc/panels.ini`，下次启动恢复。

首版直接提供 `mc` 命令，下载、安装与卸载流程尚未接入。当前支持 UTF-8 文本查看/编辑及普通文件、目录操作，没有子 Shell、mcedit、远程文件系统、归档浏览或执行文件。PNG/JPEG 文件可以用 Enter 或 F3 打开图像查看器；其他二进制文件可复制和移动，查看/编辑时会提示不支持。设计依据与范围见 [mc 设计说明](docs/mc-design.md)。

### 本地图像查看器

输入 `feh 图片.png` 打开本地图片，`feh ~/pictures` 浏览目录中的图片，也可以指定多个文件或目录。不带参数浏览终端当前目录。支持 PNG/JPEG，目录只读取直接子文件，不递归；显式文件按内容识别。摄像头通过 **Save image...** 保存到虚构电脑的图片，或玩家在游戏外放入 `runtime/world/` 的真实图片，都能直接打开。

Left / Right 切换图片，Up / Down 缩放，`/` 适应窗口，`*` 显示原始尺寸，鼠标左键拖拽或 Ctrl+方向键平移。滚轮前后切图，Ctrl+滚轮缩放；上述操作也有按钮入口。F1 查看帮助，q / Esc 退出并回到终端或 mc。窗口显示图片尺寸、格式、序号和缩放比例。

打开、切换图片或按 `r` / **Reload** 时重新读取磁盘；此前保留已载入的画面。文件被外部修改后可以重新载入，删除、损坏或格式不支持会清除旧画面并报错；恢复文件后可以再次载入。目录列表在启动时生成，新增文件需重新打开目录。查看器只读，不改写图片。首版每个文件最多20 MiB、单边16384像素、总计32000000像素；暂无动画、编辑、自动监听或远程图片功能。具体实现与边界见 [图像查看器设计说明](docs/image-viewer-design.md)。

### 纯文字浏览器

输入以下任意一条命令即可进入浏览器，退出后恢复原来的终端记录和当前目录：

```text
browser
browser https://www.daybreak.news/
browser bbs.afterhours.net
browser linnotes.me/posts/nightwalk
```

不带地址打开时显示 **Favorites** 收藏页，初始收藏包括新闻站 **Daybreak**、论坛 **After Hours** 和个人博客 **林的备忘录**。现有 36 个网页，包括论坛分区、4 个主题、19 篇帖子与 7 个用户页，以及新闻、博客、交通官网和产品专页；另有收藏页与浏览器帮助页。页面只供阅读，没有登录、发帖或下载功能。页面内容属于游戏世界，不是现实新闻。

另有交通运营官网 **环城轨道交通**，输入 `browser hcmetro.com` 或在浏览器地址栏输入 `www.hcmetro.com` 即可访问。它不加入默认收藏，也不会改动已保存的个人收藏。网站包括首页、环线时刻表、南桥站信息、公告列表与两篇运营公告；Daybreak 的延时运营报道提供了原始公告链接。

地区新闻站 **东港观察** 位于 `donggang.news`，可从 Daybreak 的“关于”页进入，包含首页、三篇东港报道和编辑部联系说明。首页的广告与堆场改造报道链接到 **明澈 MC-210 网络摄像机**（`mc210.mingche.net`）和 **远桥 R4 商用路由器**（`r4.yuanqiao.net`）两张产品专页。它们介绍设备参数、安装条件和应用项目，为后续地区调查与设备互动提供背景；当前仍为静态阅读页面。新增站点不改变初始或已保存的收藏。

| 操作 | 按键 |
| --- | --- |
| 选择上一个 / 下一个链接 | `↑` / `↓`、`Shift+Tab` / `Tab` |
| 打开选中的链接 | `Enter` / `→`，或鼠标点击 |
| 按编号打开链接 | 输入数字（支持多位数）后按 `Enter`；`Esc` 取消 |
| 后退 | `←`、`Backspace`、`Alt+←` |
| 前进 | `Alt+→` |
| 输入地址 | `G`、`Ctrl+L`（macOS 也可用 `Command+L`），输入后回车 |
| 取消地址输入，回到页面 | 地址框里按 `Esc` |
| 返回收藏页 | `H`，或顶部 **[Home]** |
| 添加 / 移除当前页收藏 | 顶部 **[+]** / **[*]**，或 `Ctrl+D` / `Command+D` |
| 刷新当前页面 | `R`、`F5`，或顶部刷新按钮 |
| 查看浏览器帮助 | `F1` / `?`，打开内部页面 `about:help` |
| 滚动长页面 | 鼠标滚轮、`PageUp` / `PageDown` / `Space`；`Home` / `End` 到页首 / 页尾 |
| 退出回到终端 | 页面中按 `Q` / `Esc`，或顶部 **[Q]** |

地址使用普通网址样式，例如 `https://www.daybreak.news/city/late-train`，也可省略协议输入 `daybreak.news`。这些域名由游戏内部路由到内置网页，不访问真实互联网或 DNS；地址中的 `https://` 只是模拟地址格式。未知域名显示连接失败，已知站点中不存在的路径显示 404。收藏页使用浏览器内部地址 `about:bookmarks`。旧版 `intranet://` 地址和已有历史会自动映射到对应的新网站。

收藏、访问记录和当前位置保存在 `runtime/browser.json`，保留最近 100 条历史和最多 100 条收藏。每次不带地址打开都会进入收藏页，之前访问过的页面仍可通过后退访问；本次打开期间，前进、后退会恢复已访问页面的链接选中和滚动位置；关闭后不保留阅读位置。这些个人数据由 `.gitignore` 排除。测试使用独立的临时目录，不访问你的浏览记录或运行文件。

浏览器采用紧凑的标题、网址与状态栏，按等宽正文的字号显示标题，使用反色突出当前链接。底部显示目标地址、链接位置和实际滚动百分比。论坛使用主题表格、分区、用户资料、楼层与引用；新闻页保留署名、更新时间、时刻表和更正；博客使用简短的文章目录和正文。参考资料和实现范围见 [浏览器设计依据](docs/browser-design.md)。

`assets/sites/pages.json` 定义初始收藏和网址清单；每个网页的 `document` 指向同目录下的独立 HTML 文件。文档使用格式良好的 XHTML 子集，可用标题、段落、列表、表格、引用与行内链接。显示时由浏览器统一处理字体、链接编号和高亮，不执行脚本、不加载外部资源。网站各自拥有页面结构，不再统一套用“几段正文加底部链接”的模板。增加网页时，同时在 CMake 资源列表中登记对应文件并重新构建。修改初始收藏不会覆盖已保存的个人收藏。

`cp` 和 `mv` 的目标如果是目录，会放入该目录并保留原文件名；目标文件已存在时会报错，不覆盖。删除目录需要明确使用 `rm -r`。当前所在目录、它的父目录和世界根目录不能被删除或移动。当前复制、移动和删除命令接受一个源或目标组合，不支持通配符；`cat` 可以读取多个文件。输出重定向支持普通内置命令，引号里的 `>` 和 `>>` 是普通文字。

快捷键：

- **↑ / ↓**：浏览历史命令，并保留尚未执行的输入。
- **Tab**：补全光标所在的命令或路径，保留后面的参数；支持带引号、反斜杠和空格的文件名，以及管道后的命令和重定向路径。多个候选项会显示出来。
- **Ctrl+R**：搜索已保存的命令历史，支持中文输入；继续按 `Ctrl+R` 找更早的匹配。`Esc` 接受结果后继续编辑，`Enter` 直接执行，`Ctrl+G` 或搜索框中的 `Ctrl+C` 取消并恢复原草稿。红色搜索框表示没有更早的匹配。
- **Ctrl+A / Ctrl+E**：移动到当前输入的开头 / 末尾；macOS 的 Command+A 仍选中当前输入。
- **Alt+B / Alt+F**：按词移动光标。
- **Ctrl+U / Ctrl+K / Ctrl+W**：剪切光标前的内容 / 光标后的内容 / 前一个以空白分隔的词；连续剪切会合并。
- **Ctrl+Y**：插回最近剪切的内容，独立于系统剪贴板；macOS 的 Command+K 继续清屏。
- **Home / End**：移动到当前输入的开头 / 末尾。
- **Ctrl+L**：清屏；macOS 也可使用 Command+K。
- **Ctrl+C**：选中文字时复制；没有选区时取消当前输入。
- **Ctrl+V**：粘贴；macOS 使用 Command+V。多行内容会合并为一行，仍需手动回车执行。
- **F1**：显示帮助。
- 鼠标拖动可选择历史输出；右键菜单支持复制和粘贴。

现在终端中的文件对应 `runtime/world/` 下的真实目录和文件。游戏路径 `/home/guest/notes.txt` 对应 `runtime/world/home/guest/notes.txt`；可直接用系统编辑器查看、编辑这些文件，下一次 `cat` 会读到修改。命令仍由游戏实现，不调用系统 Shell；路径始终限制在世界目录里，不支持符号链接和目录联接。

游戏外操作同一批世界文件是正式支持的使用方式。已有世界建立后，即使不启动程序，也可以用 Windows / macOS 的终端、文件管理器、编辑器或脚本创建、编辑、复制、移动和删除其中的文件；再次进入游戏后按实际磁盘内容读取。文本文件使用 UTF-8，文件在游戏内与游戏外的路径不同，但指向同一份内容。

当前 `cat`、`ls`、`find`、`head`、`tail` 等操作按需读取磁盘；mc 可以用 `Ctrl+R` 刷新列表。已经打开的 Vim、分页器和浏览器页面不会自动跟随外部文件变化；Vim 保存前检查内容冲突，通过 `:e` / `:e!` 明确重新读取。网页仍来自构建时内嵌的资源，尚未作为玩家世界中的可编辑网站文件提供；后续设备与软件配置的外部修改如何影响模拟状态，需要随具体功能实现。

文件修改、创建和删除会立即保存；当前目录记录在 `runtime/session.json`，下次启动继续使用。命令历史保存在 `runtime/history.json`，立即保存最近至多 1000 条命令（文件上限 4 MiB），忽略空输入与连续重复；超过 16384 字符的命令不加入历史。用 `history [数量]` 查看，`history -c` 清空。历史只保存命令文字；读取失败或文件损坏时会给出提示，保留原文件并继续使用终端，明确清空后可以重建历史。已有世界不会在重启或重新编译时被初始内容覆盖，删除过的初始文件也不会自动重建。

## 运行数据与 Git

在当前项目中运行时，本地电脑数据位于项目根目录的 `runtime/` 下：`runtime/world/` 保存玩家使用、修改后的文件，`runtime/session.json` 保存当前目录，`runtime/history.json` 保存命令历史，`runtime/browser.json` 保存收藏和浏览记录，`runtime/account.json` 保存本地账户与上次成功登录时间。整个目录已被 `.gitignore` 排除，普通的 `git add .` 不会将其加入提交。已有的 `build/` 和 `dist/` 也继续忽略。

初始内容位于 `assets/initial-world/`，纳入版本管理并在构建时嵌入程序。登录成功且 `runtime/world/` 不存在时，程序从初始内容建立一份运行副本；之后只读写运行副本。修改模板后需重新构建，模板更新只影响尚未建立世界的新副本。

把打包程序复制到项目之外时，会在程序所在目录创建 `runtime/`；macOS 则放在 `.app` 旁边。请放在可写文件夹中使用，更新时保留原有的 `runtime/`，备份电脑状态也只需复制这个目录。

`.gitignore` 不会自动取消已经跟踪的文件；当前 `runtime/` 下没有被 Git 跟踪的文件。请勿使用 `git add -f runtime/` 强制加入运行数据。自动测试使用临时世界，不会修改你的运行文件。

## Windows 开发与构建

需要 Qt 6.5 或更新版本的 **MinGW 桌面套件**、对应的 MinGW 编译器、CMake 和 Ninja。Qt 安装器可以安装这些工具。

使用 Qt Widgets、Network 和 Concurrent 模块；Network 提供密码派生函数，登录不访问网络，Concurrent 用于后台密码校验。

项目提供的脚本会在 `D:\Qt`、`C:\Qt` 和用户目录下的 `Qt` 中查找安装，只对构建进程调整环境变量，避免与 Anaconda 的 Qt 混用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

也可以明确指定套件：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -QtRoot D:\Qt\6.10.2\mingw_64 -MinGwRoot D:\Qt\Tools\mingw1310_64
```

脚本依次进行配置、编译、自动测试，并使用 `windeployqt` 生成运行文件夹。编译输出位于 `build\windows`，发布文件位于 `dist\SillyTerminal`。

使用 Qt Creator 时：打开根目录的 **CMakeLists.txt**，选择与 Qt 配套的 **Desktop Qt 6 MinGW 64-bit** Kit，点击配置、构建、运行。不要选择 Anaconda 的 Qt 或另一套 MinGW。

## macOS 开发与构建

安装 Xcode Command Line Tools、Qt 6 的 macOS 桌面套件和 CMake，然后使用 Qt Creator 打开 `CMakeLists.txt`，选择 macOS Desktop Kit 即可。

也可以在终端执行以下命令（替换为实际 Qt 路径，并确保 `cmake` 和 `ctest` 可用）：

```bash
cmake -S . -B build/macos -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.2/macos" -DCMAKE_BUILD_TYPE=Release
cmake --build build/macos --parallel
ctest --test-dir build/macos --output-on-failure
"$HOME/Qt/6.10.2/macos/bin/macdeployqt" build/macos/SillyTerminal.app
open build/macos/SillyTerminal.app
```

Windows 版本已经在当前电脑实际编译、测试。macOS 源码兼容性已考虑，但尚未在 Mac 上实测；公开发布前需要在对应系统上测试并处理签名等发布配置。

## 项目结构

```text
src/main.cpp             窗口、配色与程序入口
src/sessionwidget.*      登录页与已认证终端会话的切换
src/loginwidget.*        文字登录、隐藏密码输入与后台验证
src/accountstore.*       本地账户、密码派生与登录时间持久化
src/aiservices.*         AI 服务配置、凭据引用与异步连接检查
src/aicredentials.cpp    Windows Credential Manager / macOS Keychain 适配
src/aisettingsdialog.*   AI 服务配置界面
src/localidentity.h      登录、终端和会话栏共用的身份信息
src/terminalappearance.* 登录页与终端共用的默认字体和外观读取
src/terminalwidget.*     连续终端界面、输入保护、历史与补全交互
src/vimeditor.*          终端内的简易 Vim，模式、编辑与搜索
src/textpager.*          只读分页器，滚动、搜索和返回终端
src/mcfilemanager.*      mc 双栏文件管理器，查看、Vim 编辑与文件操作
src/browsercore.*        伪域名路由、收藏、历史导航和持久化
src/textbrowser.*        纯文字浏览器界面、链接和键盘操作
src/terminalcore.*       命令解析、受限文件读写和持久化
src/terminaltools.cpp    文件查找、文本开头和末尾截取
assets/initial-world/   初始电脑内容，构建时嵌入程序
assets/sites/pages.json 本地网站的页面内容与链接
tests/core_tests.cpp     命令和文件系统行为测试
tests/texttools_tests.cpp 文件查找、节选阅读与外部编辑保存检查
tests/account_tests.cpp  密码校验、账户持久化、损坏与并发保护测试
tests/ai_services_tests.cpp  AI 配置、模拟连接、超时、取消与凭据隔离测试
tests/session_tests.cpp  首次设置、登录重试、退出与数据保留测试
tests/mc_tests.cpp       mc 操作、文件持久化与终端/Vim/分页器衔接测试
tests/vim_tests.cpp      Vim 按键、保存、退出和持久化测试
tests/terminal_interaction_tests.cpp  历史搜索、输入编辑、补全与分页交互测试
tests/browser_tests.cpp  网站路由、链接、历史和浏览器交互测试
scripts/build.ps1       Windows 构建、测试与运行依赖打包
CMakeLists.txt          Windows / macOS 共用的构建配置
```

想增加预设文件，修改 `assets/initial-world/`，并在 `CMakeLists.txt` 的资源列表中添加新文件后重新构建。模板只影响新建的世界，已有运行文件不会被覆盖。命令逻辑与界面分开，后续可以在此基础上增加游戏内容。
