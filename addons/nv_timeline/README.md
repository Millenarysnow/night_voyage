# NV Timeline

`NVTimelineService` 是一个以 C++ 为核心的后台时间轴运行时。当前实现覆盖：

- 0.1 秒整数时间单位，以及 `manual` / `realtime` 两种逻辑时钟。
- 每个 `.timeline.json` 文件包含一条时间轴；一个 Service 当前加载一份文件，多文件协作暂列为后续扩展。
- 启用/禁用轨道、同轨道不重叠的纯视觉 `clips`，以及稳定顺序的 `output` / `blocking` 标记；`output.wait_for_result` 可等待业务返回成功或失败。
- 运行期间的 `listeners` 全程输入；暂停和阻塞等待期间仍然接收标签。
- 统一 FIFO 请求队列、Callable 程序绑定和单轮派发上限。
- 严格 JSON 字段白名单、文档级 ID 计数器、引用与时间范围校验。

## 可视化编辑器

在 Godot 编辑器的“项目 > 工具”菜单中选择 `NV Timeline`，打开独立的原生窗口。编辑器支持新建、打开、保存 `.timeline.json`；管理时钟、当前时间轴、轨道和全程输入；在画布上创建、拖动、缩放片段及标签标记，并在属性面板修改名称、总时长、时间、标签、类型与颜色。

画布每格代表 0.1 秒。单击空白位置移动播放头；片段主体可拖动，左右边缘可调整长度；同一轨道的片段不能重叠，新增片段会自动寻找下一个空闲区间，片段可以与同轨道的标签标记处于同一时间位置。撤销/重做按钮保留最近 100 次成功编辑。删除片段或标记不会影响同轨道上的其他对象。

编辑器只修改经过 `NVTimelineDocument` 校验的 JSON。片段是纯视觉对象，不会触发业务回调；标签与程序绑定由代码管理，不在此窗口编辑。更多手工验收步骤见 [时间轴可视化编辑器验收说明](../../Docs/时间轴可视化编辑器验收说明.md)。

## 构建

根项目的 CMake 会先生成 `godot-cpp`，随后构建本插件并把库复制到 `addons/nv_timeline/bin/<platform>/`。将 `res://addons/nv_timeline/nv_timeline.gdextension` 加入项目后即可使用原生类。

## 最小运行示例

```gdscript
var service := NVTimelineService.new()
add_child(service)
service.load_json_file("res://chapter_01.timeline.json")
service.start_timeline(3)
service.advance_clock(1, 10) # 推进 1 秒
service.publish_label("puzzle_completed")
```

业务绑定通过 `service.get_event_manager().register_binding(label, callable, requires_timeline_input)` 注册；回调收到一个 Dictionary 上下文，其中包含标签、来源、时间和已接收输入 ID。等待结果的输出额外提供 `output_mark_id` 和运行 ID，业务回调完成后通过 `submit_request` 返回成功或失败。JSON 不保存 Callable、脚本路径或运行状态。`examples/basic.timeline.json` 可直接作为第一份测试编排。

事件回调中使用 `service.submit_request(request)` 统一排队发标签、时间轴控制、时钟推进、快进、跳转和输出结果。支持的 `type` 共 9 类，详细字段见 [时间轴运行时实现说明](../../Docs/时间轴运行时实现说明.md)。

当前版本以 Godot `Callable` 作为业务接入点，C++ 负责时间推进、JSON、标签路由、统一请求队列和时间轴等待结果；插件不创建或追踪项目的异步事件对象。

调度规则：同一时刻按轨道数组顺序和标记数组顺序求值；普通时钟推进遇到阻塞时停住所属时间轴并丢弃本次剩余单位；快进遇阻后保留目标，成功解除后继续；跳转会跳过目标位置及之前的标记；同一标签可以同时解除多个阻塞输入。
