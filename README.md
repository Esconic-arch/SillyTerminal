# SillyTerminal

使用 **C++17、Qt 6 Widgets、CMake** 开发的伪终端初版。在 Windows 和 macOS 上使用同一套源码，分别编译运行。

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

## 怎么操作

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
| `cat <文件>` | 读取文本文件；带空格的路径使用引号 |
| `mkdir [-p] <目录>` | 创建目录；`-p` 连同父目录一起创建 |
| `touch <文件>` | 创建空文件，或更新时间；不清空已有内容 |
| `edit <文件>` | 打开内置文本编辑器，也可以新建文件 |
| `vim <文件>` | 在终端区域中使用简易 Vim 编辑文本 |
| `browser [地址]` | 打开浏览器，省略地址进入收藏页 |
| `mv <源> <目标>` | 移动或重命名文件、目录 |
| `cp [-r] <源> <目标>` | 复制文件；`-r` 复制目录及其内容 |
| `rm [-r] <路径>` | 删除文件；`-r` 删除目录及其内容 |
| `echo [文字]` | 输出文字，也可以输入中文 |
| `echo 文字 > 文件` | 覆盖写入一行 UTF-8 文字 |
| `echo 文字 >> 文件` | 在文件末尾追加一行文字 |
| `whoami` | 查看当前用户名 |
| `clear` | 清空屏幕，保留当前位置和历史命令 |

文件操作示例：

```text
cd ~
mkdir journals
touch journals/todo.txt
echo 第一条记录 > journals/todo.txt
echo 今天继续探索 >> journals/todo.txt
edit journals/todo.txt
cat journals/todo.txt
cp journals/todo.txt journals/backup.txt
mv journals/backup.txt journals/old.txt
rm journals/old.txt
```

`edit` 打开后可输入多行中文和文字，点击 **Save** 或按 **Ctrl+S**（macOS 为 **Command+S**）保存并返回终端；点击 **Cancel** 或按 **Esc** 放弃本次编辑。新文件只在保存时创建。编辑器和 `cat` 读取 UTF-8 文本，不会把二进制或其他编码的文件自动转换后覆盖。

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
| 命令行 | `:q` | 退出；有未保存修改时会提示并停留 |
| 命令行 | `:wq` | 保存并退出；保存失败时停留在编辑器 |
| 命令行 | `:q!` | 放弃未保存修改并退出 |
| 命令行 | `:x` | 有修改时保存，然后退出 |
| 命令行 | `:help` / `F1` | 查看简短按键提示 |

命令行或搜索输入中也可以按 `Esc` 取消。新文件在 `:w` 或 `:wq` 保存后才会创建；编辑内容仍保存在 `runtime/world/` 中。原有的 `edit` 命令继续提供独立窗口编辑。

这是模拟的 Vim 常用功能子集，不是系统 Vim：暂不支持数字次数（例如 `3dd`）、可视模式、正则搜索、替换、宏、插件或另存为。搜索按普通文字匹配；`w` / `b` 使用 Qt 的单词边界。编辑窗口会把换行显示为统一的换行符，明确保存时以 UTF-8、LF 换行写入。

### 纯文字浏览器

输入以下任意一条命令即可进入浏览器，退出后恢复原来的终端记录和当前目录：

```text
browser
browser https://www.daybreak.news/
browser bbs.afterhours.net
browser linnotes.me/posts/nightwalk
```

不带地址打开时显示 **Favorites** 收藏页，初始收藏包括新闻站 **Daybreak**、论坛 **After Hours** 和个人博客 **林的备忘录**。网站里有新闻、讨论和随笔，共 11 个网页；加上收藏页共 12 页。页面只供阅读，没有登录、发帖或下载功能。页面内容属于游戏世界，不是现实新闻。

| 操作 | 按键 |
| --- | --- |
| 选择上一个 / 下一个链接 | `↑` / `↓`、`Shift+Tab` / `Tab` |
| 打开选中的链接 | `Enter` / `→`，或鼠标点击 |
| 按编号直接打开链接 | 数字 `1`–`9` |
| 后退 | `←`、`Backspace`、`Alt+←` |
| 前进 | `Alt+→` |
| 输入地址 | `G`、`Ctrl+L`（macOS 也可用 `Command+L`），输入后回车 |
| 取消地址输入，回到页面 | 地址框里按 `Esc` |
| 返回收藏页 | `H`，或顶部 **Home** |
| 添加 / 移除当前页收藏 | 地址栏旁的星标，或 `Ctrl+D` / `Command+D` |
| 刷新当前页面 | `R`、`F5`，或顶部刷新按钮 |
| 查看快捷键 | `F1` |
| 滚动长页面 | 鼠标滚轮、`PageUp` / `PageDown` |
| 退出回到终端 | 页面中按 `Q` / `Esc`，或顶部 **Quit** |

地址使用普通网址样式，例如 `https://www.daybreak.news/city/late-train`，也可省略协议输入 `daybreak.news`。这些域名由游戏内部路由到内置网页，不访问真实互联网或 DNS；地址中的 `https://` 只是模拟地址格式。未知域名显示连接失败，已知站点中不存在的路径显示 404。收藏页使用浏览器内部地址 `about:bookmarks`。旧版 `intranet://` 地址和已有历史会自动映射到对应的新网站。

收藏、访问记录和当前位置保存在 `runtime/browser.json`，保留最近 100 条历史和最多 100 条收藏。每次不带地址打开都会进入收藏页，之前访问过的页面仍可通过后退访问；不保存每页滚动位置。这些个人数据由 `.gitignore` 排除。测试使用独立的临时目录，不访问你的浏览记录或运行文件。

网页内容位于 `assets/sites/pages.json`，顶层 `bookmarks` 数组定义初始收藏，每个网页包含 `url`、`title`、`subtitle`、`paragraphs` 和 `links`。链接可以指向同站路径（例如 `/city/late-train`），也可以指向其他站点的完整伪域名地址。修改 JSON 并重新构建即可更新网站，不需要服务器；初始收藏不会覆盖已保存的个人收藏。页面数据会作为纯文字转义后显示，不执行 HTML、JavaScript 或外部资源。

`cp` 和 `mv` 的目标如果是目录，会放入该目录并保留原文件名；目标文件已存在时会报错，不覆盖。删除目录需要明确使用 `rm -r`。当前所在目录、它的父目录和世界根目录不能被删除或移动。当前每条文件命令接受一个源或目标组合，不支持通配符；输出重定向目前仅支持 `echo`，引号里的 `>` 和 `>>` 是普通文字。

快捷键：

- **↑ / ↓**：浏览历史命令，并保留尚未执行的输入。
- **Tab**：补全命令或路径；多个候选项会显示出来。第一版不补全已经带引号或反斜杠的输入。
- **Home / End**：移动到当前输入的开头 / 末尾。
- **Ctrl+L**：清屏；macOS 也可使用 Command+K。
- **Ctrl+C**：选中文字时复制；没有选区时取消当前输入。
- **Ctrl+V**：粘贴；macOS 使用 Command+V。多行内容会合并为一行，仍需手动回车执行。
- **F1**：显示帮助。
- 鼠标拖动可选择历史输出；右键菜单支持复制和粘贴。

现在终端中的文件对应 `runtime/world/` 下的真实目录和文件。游戏路径 `/home/guest/notes.txt` 对应 `runtime/world/home/guest/notes.txt`；可直接用系统编辑器查看、编辑这些文件，下一次 `cat` 会读到修改。命令仍由游戏实现，不调用系统 Shell；路径始终限制在世界目录里，不支持符号链接和目录联接。

文件修改、创建和删除会立即保存；当前目录记录在 `runtime/session.json`，下次启动继续使用。历史命令仍只保留在本次会话中。已有世界不会在重启或重新编译时被初始内容覆盖，删除过的初始文件也不会自动重建。

## 运行数据与 Git

在当前项目中运行时，本地电脑数据位于项目根目录的 `runtime/` 下：`runtime/world/` 保存玩家使用、修改后的文件，`runtime/session.json` 保存当前目录，`runtime/browser.json` 保存收藏和浏览记录。整个目录已被 `.gitignore` 排除，普通的 `git add .` 不会将其加入提交。已有的 `build/` 和 `dist/` 也继续忽略。

初始内容位于 `assets/initial-world/`，纳入版本管理并在构建时嵌入程序。首次启动且 `runtime/world/` 不存在时，程序从初始内容建立一份运行副本；之后只读写运行副本。修改模板后需重新构建，模板更新只影响尚未建立世界的新副本。

把打包程序复制到项目之外时，会在程序所在目录创建 `runtime/`；macOS 则放在 `.app` 旁边。请放在可写文件夹中使用，更新时保留原有的 `runtime/`，备份电脑状态也只需复制这个目录。

`.gitignore` 不会自动取消已经跟踪的文件；当前 `runtime/` 下没有被 Git 跟踪的文件。请勿使用 `git add -f runtime/` 强制加入运行数据。自动测试使用临时世界，不会修改你的运行文件。

## Windows 开发与构建

需要 Qt 6.5 或更新版本的 **MinGW 桌面套件**、对应的 MinGW 编译器、CMake 和 Ninja。Qt 安装器可以安装这些工具。

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
src/terminalwidget.*     连续终端界面、输入保护、历史与补全交互
src/vimeditor.*          终端内的简易 Vim，模式、编辑与搜索
src/browsercore.*        伪域名路由、收藏、历史导航和持久化
src/textbrowser.*        纯文字浏览器界面、链接和键盘操作
src/terminalcore.*       命令解析、受限文件读写和持久化
assets/initial-world/   初始电脑内容，构建时嵌入程序
assets/sites/pages.json 本地网站的页面内容与链接
tests/core_tests.cpp     命令和文件系统行为测试
tests/vim_tests.cpp      Vim 按键、保存、退出和持久化测试
tests/browser_tests.cpp  网站路由、链接、历史和浏览器交互测试
scripts/build.ps1       Windows 构建、测试与运行依赖打包
CMakeLists.txt          Windows / macOS 共用的构建配置
```

想增加预设文件，修改 `assets/initial-world/`，并在 `CMakeLists.txt` 的资源列表中添加新文件后重新构建。模板只影响新建的世界，已有运行文件不会被覆盖。命令逻辑与界面分开，后续可以在此基础上增加游戏内容。
