# 固定摄像头：实现与交接

## 当前范围

摄像头侧已实现设备认证、状态读取、按需拍摄、场景冻结、拍摄记录持久化、任务查询和重试，以及图片资源预览与导出。真实 DeepSeek / ComfyUI 生成适配尚未实现；默认服务明确返回不可用。自动测试使用独立模拟服务与合成图片。

`camera [http://address]` 是本项目的快照客户端程序，入口位于终端。固定机位设备通过虚构 HTTP 服务模型访问，不建立宿主网络连接，不宣称实现完整 HTTP、ONVIF 或设备系统 Shell。交互参考网络摄像头的账户认证、设备信息和单张 JPEG 抓拍用途。

初始设备 Hearth H1 Fixed Indoor 使用 `http://192.168.50.23`，书桌电脑与设备处于同一虚构家庭网络。首期地址路由仅支持配置中的单台 HTTP 设备、默认端口 80 和根地址；不提供真实 DNS 或网络扫描。地址、账户、权限和房间布置均可后续替换，未建立漏洞或入侵剧情。

## 运行文件与外部编辑

初始资源在 `assets/initial-network/`，首次打开客户端时原子发布为与 `world/` 同级的 `runtime/network/`。已经存在的网络目录不会重新播种。删除单个文件不会在重启后恢复。网络文件位于本地世界根目录之外，避免把远程账户和场景误当成本机文件；玩家可通过宿主编辑器正式参与编辑这些真实文件。整个 `runtime/` 继续排除在 Git 外。

| 文件 | 读取时机与作用 |
| --- | --- |
| `device.json` | 连接、状态刷新、新拍摄及重试时重新读取。定义身份、地址、在线状态、HTTP 开关、隐私模式与账户权限。 |
| `scene.json` | 每次新拍摄重新读取。定义固定画面、当前可见内容、世界时间及构图约束。重试读取已保存的快照。 |
| `captures/<UUID>.json` | 查看、查询和重试时重新读取。保存原场景快照、其摘要、尝试次数和图像任务引用。 |

当前 `device.json` 的版本为 1，设备标识为 `home-camera`。`online`、`http_enabled` 和 `privacy_mode` 是布尔值；账户使用 `pbkdf2-sha256`、100000 次、16 字节十六进制盐及32字节十六进制校验值。账号密码仅用于虚构设备；AI 服务凭据仍由宿主 AI 模块管理。

初始账户 `viewer` / `teacup-27` 可以拍照，`monitor` / `status-only` 只读设备信息。会话不保存明文密码。账户删除、校验信息变更或地址改变会要求重新登录；权限变更在下次操作生效。设备离线、服务禁用、权限不足或隐私模式在提交图像任务之前处理，不因图像服务失败改变设备在线状态。

`scene.json` 版本为 1，包括以下字段：

- `id` / `revision`：场景及版本标识，拍摄时连同具体内容保存。
- `world_time`：ISO 8601 世界时间，由文件明确给出；首期不自动推进，也不随退出后的现实时间变化。
- `view`：`id`、`description`、`width`、`height`，固定构图与16至4096范围内的画幅尺寸。
- `state`：`door` 和 `curtains` 取 `open` / `closed`；`lamp` 取 `on` / `off`。
- `objects`：数组，每项具有 `id`、`description`、`location`、`visible`。
- `people`：同上，另有 `action`；只有 `visible: true` 的人物进入图片上下文。
- `constraints`：必须保留的画面约束字符串数组。
- `references`：参考素材的 `id` 与 `version` 数组；当前初始数组为空，尚未提供房间基准图。

每次拍摄只向图像接口传递固定画面、可见事实、数量、约束与参考素材引用，排除设备密码及不可见人物。当前可见性由显式区域描述与 `visible` 字段指定，没有三维遮挡计算或自动人物事件模拟。

无效、缺失或大于1 MiB的结构化文件会报错，保留原内容，不使用静默缓存回退；符号链接和目录联接被拒绝。网络层不回写设备与场景文件。新拍摄使用独立记录；更新记录使用协作锁、原内容比较和原子替换，检测到读取之后的外部编辑则终止写入。未知的记录扩展字段在之后更新时保留。外部编辑器不遵守协作锁时，最终比较与替换之间仍有短暂竞态，建议在非操作期间编辑拍摄记录。

拍摄快照和摘要不匹配时拒绝继续使用该记录，避免修改后的事实与已生成图片被当成同一张照片；可通过编辑 `scene.json` 后新拍照创建新事实。拍摄记录删除后不会自动重建；存在的 AI 资源不会因此被自动删除。

## 图像接口 v1

摄像头侧的具体边界位于 `src/cameraimageprovider.h`。它是供 AI 方向实现的适配入口，尚不意味着真实生成服务已完成。`CameraDialog` 和 `CameraCore` 均允许注入 `std::shared_ptr<CameraImageProvider>`；程序默认使用不可用实现。

所有方法应迅速返回本地状态。真实适配器在其内部执行异步网络请求、保存任务与图片资源；`CameraCore` 和 UI 不读取 AI 密钥、组装 DeepSeek 请求或理解 ComfyUI 工作流。

| 方法 | 摄像头侧的调用约定 |
| --- | --- |
| `submit(snapshot)` | 接受冻结快照，按 `capture_id` 去重；相同标识与不同内容应报告冲突。 |
| `query(taskId)` | 查询或恢复现有任务，不能自动重新提交。 |
| `recover(captureId)` | 只读恢复：处理提交已经开始、拍摄记录还未来得及保存任务 ID 的崩溃窗口。不能因找不到 ID 盲目重提。 |
| `retry(taskId, snapshot)` | 对已确认失败任务进行下一次尝试，沿用原标识与快照。 |
| `readImage(resourceId)` | 读取已经保存的图片字节及 `png` 或 `jpeg` 格式。资源不以宿主路径暴露给玩家。 |

快照实际结构为：

```json
{
  "schema_version": 1,
  "capture_id": "每次新拍摄的UUID",
  "camera_id": "home-camera",
  "world_time": "2026-10-20T19:42:00+08:00",
  "scene": {
    "id": "living-room",
    "version": "1",
    "view": {
      "id": "living-room-fixed",
      "description": "固定机位与家具布局",
      "width": 1920,
      "height": 1080
    },
    "visible_facts": [
      {"id": "lamp", "description": "Floor lamp is on."},
      {"id": "people-count", "description": "Visible people: 0."}
    ],
    "constraints": ["固定构图及数量约束"],
    "references": []
  }
}
```

任务返回 `id`、`state`、`resourceId`、`message`。状态为 `queued`、`generating`、`completed`、`failed` 或 `uncertain`。排队和生成中必须有任务 ID；完成必须有任务 ID 与资源 ID。错误消息应已经由 AI 层处理为可显示的信息。摄像头层检查字段和状态；无法解释的响应保留为 `uncertain`，不自动重提。

拍摄记录先保存为 `prepared`，表示尚未提交；提交前保存为 `uncertain` 并增加尝试次数，再调用服务。崩溃后，`prepared` 可由玩家明确提交，`uncertain` 通过任务 ID 或拍摄 ID 恢复。已确认失败可重试；完成后重新打开直接读取原资源。默认不可用服务返回已确认失败且无任务 ID，明确重试时重新调用相同拍摄 ID 的 `submit`。

窗口每750毫秒查询当前选中的排队或生成中记录。切换记录或关闭窗口仅停止本地观察，不取消远端任务；重新选中待处理记录可继续查询。状态不确定时保留记录，由 **Refresh status** 明确恢复。

图片资源由 AI 方向持久化。客户端限制图片内容至20 MiB，预览前检查尺寸和解码；导出通过 `TerminalCore::saveNewBinaryFile` 写入本机世界中的真实文件，使用仅新建方式保留已有文件。摄像头状态与拍摄快照不因图片生成结果变化而改变。

## 验证与参考

`tests/camera_tests.cpp` 使用临时网络目录、临时世界和测试专用图片服务，覆盖认证、权限即时变化、源文件外部更新、缺失与损坏、原快照重试、重启恢复、提交中断后的只读查找、写入冲突、图片显示和真实文件导出。测试服务不链接到游戏的生产默认实现，不调用外部服务。

- [ONVIF Media Service](https://www.onvif.org/specs/srv/media/ONVIF-Media-Service-Spec-v242a.pdf)：GetSnapshotUri 与通过 HTTP 获取单张 JPEG 的用途。
- [Reolink 单张 JPEG 抓拍说明](https://support.reolink.com/articles/360007011233-How-to-Capture-Live-JPEG-Image-of-Reolink-Cameras-via-Web-Browsers/)：参考账户、HTTP 服务开关与单次抓拍行为。项目未复刻该产品 CGI 接口。
