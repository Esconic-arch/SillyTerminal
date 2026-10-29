# 摄像头生成适配：DeepSeek → ComfyUI

## 实现与使用

`AiCameraImageProvider` 实现 `CameraImageProvider` v1：接收冻结场景，调用DeepSeek生成JSON画面描述，填入ComfyUI API工作流，查询队列和历史、下载PNG/JPEG，并保存为图片资源。`SessionWidget` 持有共享适配器，经终端传入拍照窗口；关闭窗口或退出游戏内账户不取消后台任务。应用退出后按持久化状态恢复。

首期支持摄像头专用预设及字面输入绑定，不解释SillyTavern宏或扩展脚本。快照中的 `references` 非空时在调用DeepSeek前报错，避免忽略参考素材后冒充一致画面。初始场景尚无基准图，纯文生图的空间、人物和线索一致性仍需实际样本验证。

配置步骤：

1. 打开右上角 **AI → Connections**，配置并检查DeepSeek和自托管ComfyUI。连接检查不生成内容。
2. 在ComfyUI验证一份能生成单张PNG/JPEG的工作流，导出为 **API format**。编辑器布局JSON不能直接使用。
3. 在 **Camera generation → Export example...** 导出示例。替换其中的checkpoint和 `workflow`，按实际节点填写 `bindings` 与 `output_node`。示例的 `REPLACE_WITH_...` 占位符会被拒绝；程序不会安装模型。
4. **Browse...** 选择编辑后的profile，**Check profile** 检查本地格式，再 **Save**。格式检查不证明远端模型、节点或显存可用。
5. 登录终端，执行 `camera 192.168.50.23`，以虚构设备账户登录后点击 **Take snapshot**。这个动作调用真实服务，结果通过已有预览和导出功能使用。

缺少配置或服务失败时保留记录并显示原因，不以模拟图替代。修正配置后，尚未取得有效profile的失败任务可明确重试；已取得profile的任务保留其原版本，文件修改供新拍摄使用。

## Profile格式

完整示例位于 `assets/ai/camera-profile.example.json`，也内嵌在程序中。以下为结构示意，`workflow` 必须替换后才有效：

```json
{
  "version": 1,
  "width": 1024,
  "height": 576,
  "instructions": "Optional visual style guidance.",
  "output_node": "9",
  "bindings": {
    "prompt": { "node": "6", "input": "text" },
    "negative_prompt": { "node": "7", "input": "text" },
    "width": { "node": "5", "input": "width" },
    "height": { "node": "5", "input": "height" },
    "seed": { "node": "3", "input": "seed" }
  },
  "workflow": { "replace-this": "Insert your exported API graph" }
}
```

节点编号没有固定要求。绑定必须指向存在的字面输入，不能覆盖节点连接。`prompt`、`width`、`height`、`seed` 必填；`negative_prompt` 可省略。输出节点必须在历史结果中提供恰好一张保存在输出或临时目录的图片。

`width` / `height` 是工作流实际生成尺寸，范围16至4096，比例必须与摄像头画幅一致。结果本地等比转换到请求尺寸，例如1024×576到1920×1080；这是尺寸转换，不代表额外的原生细节。不同画幅的输出会报错，不会悄悄裁切。

默认任务预设要求保留机位、可见事实、数量、位置和约束，返回 `prompt`、可选 `negative_prompt` 和 `used_fact_ids`。`instructions` 提供附加视觉风格，默认JSON输出契约继续保留。工作流的正面提示词还会附上程序保存的固定机位、可见事实和约束。JSON和引用校验不能证明自然语言语义或图片内容一定正确。

可在 **AI → Text preset** 选择独立文本预设，也可留空等待以后加入。原生 v1 的模块、示例消息、预览与兼容限制见[文本预设接口](ai-prompt-presets.md)。预设和完整消息在拍摄提交时一起冻结到任务的 `text_request` 与摘要字段；重试不读取修改后的源预设。旧任务缺少这些字段时继续使用内置指令，不附加当前新选的预设。

DeepSeek使用所选模型的非流式Chat Completions、JSON object输出、2048输出token上限及关闭思考模式。空结果、截断、错误或缺失事实引用会在图片提交前报错。设备密码、不可见人物及聊天历史不进入此请求。

## 文件与外部编辑

AI设置保存profile的宿主绝对路径。新拍摄重新读取该文件，在提交返回前保存其具体内容和摘要；排队期间修改源文件不影响原任务。profile限4 MiB，不执行其中的脚本。

每个世界使用自己的真实运行文件：

```text
runtime/world/                    玩家文件
runtime/network/captures/ID.json   摄像头拍摄记录
runtime/ai/jobs/ID.json            AI任务、快照、profile、描述、工作流和远端引用
runtime/ai/resources/ID.png        保存后的照片
```

AI任务与资源ID使用拍摄UUID。任务保存服务地址和模型名，不保存服务设置中的真实密钥；凭据按实际请求需要从系统存储读取。恢复时要求当前服务地址与记录一致，避免把旧任务查询发往另一台服务。

查询和重试重新读取任务文件。缺失、快照或profile摘要不匹配时明确报错并保留文件。更新使用协作锁、原内容比较和原子替换；读取之后的外部编辑会阻止覆盖并停止该写入者。遇到这种冲突时，重新启动应用以重新读取记录，再通过刷新状态恢复原远端任务，未知扩展字段会保留。外部编辑器不遵守协作锁时，最终比较与替换之间仍有短暂竞态，建议在非操作期间编辑记录。

图片每次读取磁盘。外部替换为合法PNG/JPEG后，下次读取会反映新内容；缺失、损坏、尺寸超过4096或大于20 MiB时报错，不自动补图。生成结果遇到内容不同的已有文件时报告冲突，不覆盖。

## 状态、重试和恢复

- `submit` 先保存任务，再依次调度异步请求。同一拍摄与相同快照返回原任务，不同快照使用相同ID时报冲突。
- 内部阶段包括描述、提交、队列等待和下载。窗口读取本地状态，不因刷新而新建生成任务。
- DeepSeek绝对超时默认120秒；ComfyUI单次请求使用连接超时设置。队列后台等待默认最多10分钟，之后保留 `uncertain`，可明确刷新。
- 提交ComfyUI前保存预分配的远端UUID。服务器确认另一合法ID时保存其值。提交超时、断线或服务器错误导致接收状态不明时，只查询队列和历史，不自动POST。旧服务忽略客户端ID且确认丢失时，可能无法恢复。
- 应用在文字生成或尚未启动的本地排队阶段退出，恢复时标记失败，要求明确重试。已经保存的文字描述会复用；查询不会偷偷重做文字生成。
- 确认图片生成失败后的明确重试使用原快照、profile、提示词和已有种子。下载失败只重做GET。已完成照片直接读盘。

本地去重不证明外部服务一次且仅一次执行；固定种子也不保证不同模型、软件或硬件下的图像完全一致。

## 验证范围

`ai_camera_tests` 使用临时目录、合成密钥和回环HTTP假服务，经过实际请求序列化、描述解析、工作流填参、状态查询、下载与图片编码，并与真正的 `CameraCore` 联调。覆盖重复提交、立即冻结profile、原状态重试、只重试下载、提交不确定、重启恢复、外部文件编辑、认证失败和无效输出等路径。

真实DeepSeek与ComfyUI模型的画面效果尚未验证，macOS仍需实机检查。第一轮真实联调需要一份已在目标ComfyUI运行成功的API工作流。

依据：[DeepSeek Chat Completions](https://api-docs.deepseek.com/api/create-chat-completion/)、[JSON输出](https://api-docs.deepseek.com/guides/json_mode/)、[ComfyUI API示例](https://docs.comfy.org/development/comfyui-server/api-examples)、[HTTP路由](https://docs.comfy.org/development/comfyui-server/comms_routes)。
