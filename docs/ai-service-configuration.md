# AI 服务配置与连接检查

## 已实现的范围

本轮提供 DeepSeek 和自托管 ComfyUI 的真实服务配置、系统凭据保存，以及异步、只读的连接检查。窗口右上角的 **AI** 按钮打开设置；登录前也可以配置。保存和打开设置都不会自动连接服务。

这层基础已用于摄像头生成：在 **Camera generation** 页选择任务profile后，摄像头可调用DeepSeek整理画面、提交ComfyUI工作流并保存照片。配置格式、限制和验证范围见[摄像头生成适配](ai-camera-generation.md)；摄像头侧接口见[实现与交接](camera-implementation.md)。通用人物聊天及SillyTavern预设导入仍待后续任务。

## 如何配置

DeepSeek 填写 API 根地址、API key 和模型名称。官方根地址默认是 `https://api.deepseek.com`；可使用包含 `/v1` 的兼容地址，不要把 `/chat/completions` 填入根地址。默认模型名称可编辑；以当前服务实际返回的模型列表为准。

点击 **Test connection / load models**，程序携带密钥请求 `GET <base>/models`。成功后模型列表会出现在下拉框中；如果原来填写的模型未被返回，界面会提示重新选择。连接检查只确认 API 可访问和模型列表可读取，不证明所选模型已完成文本生成，也不确认余额是否足够。

ComfyUI 填写正在运行的自托管服务器地址，默认 `http://127.0.0.1:8188`。支持远程地址和反向代理的路径前缀。普通本地服务的 Bearer token 留空；只有服务部署明确需要这种认证时才填写。此版本不提供 Comfy Cloud 的独立 API 协议适配。

点击 **Test connection** 请求 `GET <base>/system_stats`，检查是否返回 ComfyUI 的系统与设备信息。该操作不启动或安装 ComfyUI，不提交图片任务，也不验证某个工作流、节点或模型资源是否齐全。

连接检查使用当前正在编辑的配置。点击 **Save** 才保存；**Cancel** 放弃未保存的配置和密钥。空密钥表示不保存该服务的密钥，可用 **Clear** 明确清除。正在检查连接时仍可以取消并关闭设置，待处理的本地请求会中止。

## 配置与凭据

普通配置保存到 Qt `QStandardPaths::AppConfigLocation` 下的 `ai-services.json`，属于宿主应用用户设置，不在游戏的 `runtime/world/` 中。Windows 和 macOS 分别使用各自的用户配置目录，不依赖当前工作目录。应用名称为 `SillyTerminal`，组织名为 `Esconic`。

文件包含版本、服务地址、模型名称、连接超时和不透明凭据引用。API key 和可选 ComfyUI token 使用 Windows Credential Manager 或 macOS Keychain 保存，不写入 JSON、命令历史、游戏文件或 Git。不提供失败后改存明文的回退。

保存先创建新的凭据引用，再原子替换配置；写入失败时回收新引用，保留原配置和旧凭据。成功后清理旧引用。配置损坏或版本不支持时显示错误并保留文件。已有凭据无法读取时，用户必须重新输入或明确清除，避免保存其他字段时静默删除原引用。

应用只在用户打开服务设置、进行后续服务操作时读取对应的凭据，不在启动时自动检查服务。不同工作目录运行的同一应用共享宿主用户的服务配置；游戏的账户与玩家文件仍各自隔离。

## 程序内入口

- `AiSettingsStore::load`：读取非密钥配置；`readKey` 通过凭据引用读取服务密钥；`save` 保存一份设置草稿。默认使用系统存储，测试可以注入临时路径和假的凭据存储。
- `AiConnectionClient::checkDeepSeek`：异步检查模型列表。
- `AiConnectionClient::checkComfyUi`：异步检查自托管服务器。
- `AiConnectionClient::onFinished`：完成回调，返回 `AiConnectionResult`，包括是否成功、可显示的消息和适用时的模型列表。使用项目已有的回调方式，兼容当前 Windows Qt 套件在含中文路径中的构建。
- `AiConnectionClient::cancel`：中止当前检查并断开当前请求的回调，不发送完成通知；保留 `onFinished` 供后续检查使用。该类每个实例同时承载一次检查；新检查会替换前一个。
- `AiSettingsDialog`：宿主应用的配置界面。网络、网站等游戏模块不直接读取服务密钥；后续通过 AI 生成任务接口使用结果。

检查设置绝对超时和 1 MiB 响应上限，不自动重试，也不跟随 HTTP 重定向。错误反馈区分认证拒绝、地址错误、限流、超时和 TLS 问题。服务返回的原始错误文本不直接显示，避免把反射的密钥或其他数据带到界面和日志中。

## 验证范围

`ai_services_tests` 使用临时配置、内存凭据和回环地址 HTTP 服务检查请求路径、认证、模型列表、错误响应、重定向、超时、响应上限及设置的保存、取消和恢复。常规测试不访问真实服务，也不读取用户凭据。

运行这些网络检查需要允许本地回环通信；禁止进程网络访问的沙箱可能同时阻止模拟客户端访问本地测试服务器，应在允许回环通信的测试环境完整执行全部检查。

应用冒烟检查还会从主窗口打开并取消 AI 设置，使用隔离世界旁的空配置，检查默认密钥为空、取消不保存，以及运行包能加载 TLS 后端。该检查不读取用户的 AI 配置或已有凭据，也不发起真实服务请求。

可单独运行 `ai_services_tests --native-secret-check`，在系统凭据存储创建一个随机名称的合成测试条目，验证读写后立即删除；它不读取任何既有条目。Windows 已实际检查；macOS 凭据实现仍需在 Mac 上实测。真实 DeepSeek 和 ComfyUI 的连接结果需要用户自行配置服务后检查。

## 依据

- [DeepSeek 首次调用](https://api-docs.deepseek.com/)：根地址与 Bearer API key。
- [DeepSeek 模型列表](https://api-docs.deepseek.com/api/list-models/)：只读模型发现接口。
- [ComfyUI 自托管服务接口](https://docs.comfy.org/development/comfyui-server/comms_routes)：`/system_stats` 等服务路由。
- [Qt NetworkRequest](https://doc.qt.io/qt-6/qnetworkrequest.html)：显式重定向策略。
- [Windows 凭据读取](https://learn.microsoft.com/windows/win32/api/wincred/nf-wincred-credreadw)与 [Apple Keychain](https://developer.apple.com/documentation/security/ksecclassgenericpassword)：系统凭据存储。
