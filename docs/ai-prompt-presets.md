# AI 文本预设接口

本接口为以后加入预设保留独立入口。当前摄像头的 DeepSeek 描述步骤已接入；ComfyUI API 工作流继续由 camera profile 配置。预设文件只负责附加消息，不改变世界事实、接口地址、密钥、输出解析或工作流绑定。

## 现在可以怎样配置

打开右上角 **AI → Text preset**：

1. 留空即可使用现有内置摄像头指令，不需要先取得社区预设。
2. **Export template...** 导出可直接校验和保存的空模板，全部模块默认关闭。模板也位于 `assets/ai/prompt-preset.example.json`。
3. 在外部编辑器填写模块内容，将所需模块的 `enabled` 改为 `true`。用 **Browse...** 选择文件。
4. **Check / preview** 在本地校验，并显示组装后的消息；预览使用明确标注的示例输入，不代表当前世界画面，不会调用服务。若已选择 camera profile，预览会包含其视觉风格指令。
5. **Save** 保存选择；**Use built-in** 清空预设选择，保存后供新拍摄使用。Cancel 放弃草稿。导出文件本身是显式磁盘操作，不会因 Cancel 删除。

AI 设置文件增加可选的 `camera_text_preset` 绝对路径，旧配置缺少该字段时视为留空。保存设置前会完成文件校验和试组装，失败不会写入设置或新凭据。连接、ComfyUI 工作流及真实密钥仍须按[摄像头生成配置](ai-camera-generation.md)配置。

## 原生格式 v1

```json
{
  "version": 1,
  "name": "My camera preset",
  "modules": [
    {
      "id": "visual-style",
      "enabled": true,
      "role": "system",
      "position": "before_input",
      "content": "Use the concise visual language of a fixed indoor security camera."
    },
    {
      "id": "additional-guidance",
      "enabled": false,
      "role": "user",
      "position": "after_input",
      "content": ""
    }
  ]
}
```

- `name`：1–200 字符的非空名称，仅用于识别预设。
- `modules`：最多 256 项；每项需要 `id`、`enabled`、`role`、`position`、`content`。
- `id`：不重复的 1–64 个英文字母、数字、下划线、点或连字符。
- `role`：`system`、`user` 或 `assistant`。`assistant` 项是完整示例回复，不启用续写前缀 API。
- `position`：`before_input` 或 `after_input`。system 项只能使用 `before_input`。
- `content`：每项最多 65,536 个 UTF-16 代码单元；启用时必须非空，关闭的空项可以等待以后填入。

明确的组装规则：先放内置任务指令和 camera profile 的视觉指令，再按数组顺序合并所有启用的 system 模块；随后放 `before_input` 的非 system 模块、程序提供的快照、`after_input` 的非 system 模块，最后追加程序的 JSON 输出要求。两个位置组内都保留数组顺序。相邻同角色消息合并，system 仅出现在开头。示例对话必须从 user 开始；最终消息为 user，不将最后的 assistant 示例当成不完整前缀。

快照以文字 JSON 放在 `<task_input>` 标记中，内容不进行宏展开。这个标记只是输入边界，不能保证模型一定遵守事实；结果仍经过摄像头描述解析、事实 ID 与图片格式检查。

预设文件必须是绝对路径的普通文件，不能是符号链接或 junction，最大 1 MiB。组装后消息最大 2 MiB。这些是本地容量限制，不能扩大实际模型的 token 上下文；DeepSeek 当前仍使用 JSON mode、非流式、关闭 thinking 和最多 2048 个输出 token。

## 程序接入点

`src/aipromptpreset.h` 是独立于摄像头的消息接口：

```cpp
AiPromptPreset::empty();
AiPromptPreset::validate(preset, &error);
AiPromptPreset::load(absolutePathOrEmpty, &preset, &error);
AiPromptPreset::compose(preset, taskInstructions, taskInput,
    finalOutputInstructions, &messages, &error);
AiPromptPreset::validateMessages(messages, &error);
```

`AiCameraProfile::messages` 提供摄像头具体任务指令和快照，设置预览与真实任务调用同一个组装入口。其他生成用途以后可以使用 `compose`，并传入各自的任务、输入和输出契约，不需要把摄像头逻辑复制过去。网络方向仍使用现有 `CameraImageProvider`，无需读取预设或真实服务设置。

后续 SillyTavern 导入器应把可支持的模块、角色、启用状态和顺序转换成这一原生格式，并报告不支持的能力。当前不接受包含 `prompts` / `prompt_order` 的酒馆整包，不展开 `{{...}}` 宏，也不执行正则、STscript、Tavern Helper 或其他扩展。启用模块内出现未展开的双花括号宏会报错；未知结构字段也会报错，不会静默忽略。这个版本不承诺社区“破限”效果。

## 冻结、重试与外部编辑

每次新拍摄在 `submit` 返回前重新读取所选文件，完成预检查，再把原生预设和已组装的完整消息存入 AI 任务的 `text_request`，同时保存 `text_request_sha256`。不在初始化、打开设置、预览、登录或历史查询时自动生成。

修改或删除源预设只影响后续新任务；已排队的任务和重试使用保存的消息，不重新选择预设。选中的文件缺失、损坏或不兼容时，新任务在读取凭据及调用 DeepSeek 前明确失败，不悄悄退回默认。修好文件后，未取得有效生成配置的失败任务可以显式重试。

之前版本的任务没有 `text_request` 时，不把当前新选的预设附加到旧拍摄上。若该任务需要重新请求文本，在显式重试时为其保存内置任务指令；已有描述或已完成资源继续复用。

任务读取会检查消息结构、原生预设和摘要。运行文件的外部修改继续遵守[摄像头生成配置](ai-camera-generation.md)的锁、比较后保存、冲突停止与重启恢复规则；缺失或不一致时保留文件，不覆盖编辑或自动重新生成。

## 检查范围

`ai_prompt_preset_tests` 验证格式、模块顺序、角色合并、空模板、宏拒绝、文件更新及容量限制。`ai_camera_tests` 使用回环假服务验证真实请求采用冻结消息、删除源文件后重试、缺失文件在凭据访问前失败，以及外部任务修改被拒绝。`ai_services_tests` 验证设置拒绝无效文件、本地预览及草稿保存行为。测试使用合成密钥，不验证真实模型的输出或“破限”效果。

参考：[SillyTavern Prompt Manager](https://docs.sillytavern.app/usage/prompts/prompt-manager/)、[DeepSeek JSON Output](https://api-docs.deepseek.com/guides/json_mode/)。原生 v1 是本项目契约，不宣称与 SillyTavern 格式等价。
